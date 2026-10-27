"""Audit one real continuous campaign's teaching, training, and reward ledger."""
from __future__ import annotations
import argparse
from collections import Counter
from datetime import datetime,timezone
import json
from pathlib import Path
from ValidateCampaignTelemetry import Evidence,ROOT,digest,integer,number,truth


def node_id(event):
    return str(event.get('detail','')).split(';',1)[0].strip()


def audit(campaign,evidence):
    checks=[]
    def check(key,actual,expected,missing=False):
        checks.append({'id':key,'status':'missing' if missing else 'pass' if actual==expected else 'fail','actual':actual,'expected':expected})
    ids=[s['id'] for s in campaign['stages']]
    reports={r['stage']:r for r in evidence.reports}
    check('session.report_order',[r['stage'] for r in evidence.reports],ids)
    paths={r.get('_events_path') for r in evidence.reports}
    check('session.one_event_file',len(paths),1)
    events=[e for e in evidence.events if not e.get('_isolated')]
    check('session.single_begin',sum(e['event']=='session' for e in events),1)
    check('session.stage_completion_order',[e['stage'] for e in events if e['event']=='stage_complete'],ids)
    check('session.no_injection',sum(e['event'] in ('test_state_injection','state_injection','debug_mutation','force_complete') for e in events),0)
    check('session.all_mode',[r.get('mode') for r in evidence.reports],['all']*9)
    check('session.completed_stage_progress',[integer(reports.get(s,{}).get('completed_stages')) for s in ids],list(range(1,10)))
    final=reports.get(ids[-1],{})
    check('session.final_balance',[integer(final.get('astrites')),integer(final.get('shell_credits'))],[600,300000])
    check('session.formal_wave_count',sum(len(r.get('waves',[])) for r in evidence.reports),sum(len(s['waves']) for s in campaign['stages']))
    tutorials=[]
    for node in campaign['tutorials']:
        key=node['id']
        def matches_trigger(event):
            return (event['stage']==node['stage'] and integer(event['wave'])==node['wave']+1
                    and event['event']==node['event']
                    and all(str(event.get(k))==str(v) for k,v in node.get('event_fields',{}).items()))
        shown=[e for e in events if e['event']=='tutorial_show' and node_id(e)==key]
        ack=[e for e in events if e['event']=='tutorial_ack' and node_id(e)==key]
        complete=[e for e in events if e['event']=='tutorial_complete' and node_id(e)==key]
        check(key+'.shown_once',len(shown),1,not shown)
        check(key+'.acknowledged_once',len(ack),1,not ack)
        row={'id':key,'title':node['title'],'expected_stage':node['stage'],'expected_wave':node['wave']+1,
             'show':[{k:e.get(k) for k in ('stage','wave','sim_seconds','_index')} for e in shown],
             'ack':[{k:e.get(k) for k in ('stage','wave','sim_seconds','_index')} for e in ack],
             'explicit_complete':[{k:e.get(k) for k in ('stage','wave','sim_seconds','_index')} for e in complete]}
        if shown:
            show=shown[0]
            check(key+'.location',[show['stage'],integer(show['wave'])],[node['stage'],node['wave']+1])
            check(key+'.ack_after_show',bool(ack and ack[0]['_index']>show['_index']),True,not ack)
            if node['completion']=='relay_committed':
                check(key+'.real_completion_once',len(complete),1,not complete)
                if complete:check(key+'.completion_after_ack',bool(ack and complete[0]['_index']>ack[0]['_index']),True,not ack)
            if node['phase'] in ('combat','complete'):
                triggers=[e for e in events if matches_trigger(e) and e['_index']<show['_index']]
                check(key+'.observed_trigger_before_show',bool(triggers),True,not triggers)
                row['last_trigger']=triggers[-1] if triggers else None
            if key=='t04_relight':
                fault=next(f for s in campaign['stages'] if s['id']==node['stage'] for f in s['waves'][node['wave']]['faults'])
                start=[e for e in events if e['stage']==node['stage'] and integer(e['wave'])==node['wave']+1
                       and e['event']=='fault_started' and e.get('lamp')==fault['lamp_id'] and e['_index']<show['_index']]
                check(key+'.after_scheduled_fault_end',bool(start and number(show['sim_seconds'])>=fault['time']+fault['seconds']),True)
                trigger=row.get('last_trigger') or {}
                check(key+'.recovers_expected_lamp',trigger.get('lamp'),fault['lamp_id'],not trigger)
        else:
            triggers=[e for e in events if matches_trigger(e)]
            row['trigger_count_in_expected_wave']=len(triggers)
        row['status']='fail' if any(c['status']=='fail' for c in checks if c['id'].startswith(key+'.')) else 'missing' if any(c['status']=='missing' for c in checks if c['id'].startswith(key+'.')) else 'pass'
        tutorials.append(row)
    check('teaching.unique_shown',len({node_id(e) for e in events if e['event']=='tutorial_show'}),len(campaign['tutorials']))
    check('teaching.no_hazard_nodes',sum(e['event']=='tutorial_show' and str(e['stage']).startswith('hazard') for e in events),0)
    training_spawn=[e for e in events if e['event']=='training_spawn']
    training_complete=[e for e in events if e['event']=='training_complete']
    check('training.target_count',len(training_spawn),len(campaign['rules']['training']['targets']),not training_spawn)
    check('training.completed_once',len(training_complete),1,not training_complete)
    training={}
    if training_spawn and training_complete:
        first,last=training_spawn[0]['_index'],training_complete[0]['_index']
        span=[e for e in events if first<=e['_index']<=last]
        relays=[e for e in span if e['event']=='relay_committed' and truth(e.get('training'))]
        lamp_hits=[e for e in span if e['event']=='damage' and str(e.get('source','')).startswith('lamp_') and (number(e.get('amount')) or 0)>0]
        hits=[e for e in span if e['event']=='energy_hit' and (number(e.get('accepted')) or 0)>0]
        check('training.real_character_energy_hits',bool(hits),True,not hits)
        check('training.real_relay',len(relays),1,not relays)
        check('training.real_lamp_damage',bool(lamp_hits),True,not lamp_hits)
        check('training.formal_spawns_in_span',sum(e['event']=='enemy_spawn' for e in span),0)
        check('training.explicit_exclusion',truth(training_complete[0].get('excluded_from_formal_counts')),True)
        seconds=number(training_complete[0]['sim_seconds'])
        low,high=campaign['validation_targets']['first_relay_seconds']
        check('training.first_relay_in_design_window',bool(relays and low<=number(relays[0]['sim_seconds'])<=high),True)
        deploy=next((e for e in events if e['event']=='tutorial_show' and node_id(e)=='t01_deploy'),None)
        check('training.deployment_after_completion',bool(deploy and deploy['_index']>last),True,deploy is None)
        training={'completion_seconds':seconds,'relay_seconds':[number(e['sim_seconds']) for e in relays],
                  'lamp_damage_seconds':[number(e['sim_seconds']) for e in lamp_hits],'training_spawns':len(training_spawn),
                  'energy_hit_count':len(hits),'lamp_hit_count':len(lamp_hits)}
    expected_enemies=sum(b['count'] for s in campaign['stages'] for w in s['waves'] for b in w['batches'])
    check('training.excluded_from_formal_enemy_total',sum(e['event']=='enemy_spawn' for e in events),expected_enemies)
    ledger=[];astrite=credits=0;seen=set();completed=set();probes=[]
    rewards={r['id']:r for r in campaign['rewards']}
    for index,event in enumerate(events):
        if event['event']=='stage_complete':completed.add(event['stage'])
        if event['event']=='reward_claim':
            conditions=[s for s in event.get('conditions','').split(',') if s]
            valid=all(k in rewards for k in conditions)
            check(f'ledger.{index}.known_conditions',valid,True)
            if not valid:continue
            allowed=all((rewards[k].get('stage_id') in completed if rewards[k]['kind']=='stage_first_clear'
                         else set(rewards[k]['stage_ids']).issubset(completed)) for k in conditions)
            check(f'ledger.{index}.earned_before_claim',allowed,True)
            check(f'ledger.{index}.new_conditions',not (set(conditions)&seen),True)
            da=sum(rewards[k]['astrite'] for k in conditions);ds=sum(rewards[k]['shell_credits'] for k in conditions)
            astrite+=da;credits+=ds;seen.update(conditions)
            check(f'ledger.{index}.delta',[integer(event.get('astrite_delta')),integer(event.get('shell_delta'))],[da,ds])
            check(f'ledger.{index}.balance',[integer(event.get('balance_astrite')),integer(event.get('balance_shell'))],[astrite,credits])
            if conditions:ledger.append({'stage':event['stage'],'conditions':conditions,'astrite_delta':da,'shell_delta':ds,'balance_astrite':astrite,'balance_shell':credits})
        if event['event']=='reward_retry_probe':
            next_claim=next((e for e in events[index+1:] if e['event'] in ('reward_claim','stage_selected')),None)
            ok=bool(next_claim and next_claim['event']=='reward_claim' and next_claim['stage']==event['stage']
                    and integer(next_claim.get('astrite_delta'))==0 and integer(next_claim.get('shell_delta'))==0
                    and not next_claim.get('conditions') and integer(next_claim.get('balance_astrite'))==astrite
                    and integer(next_claim.get('balance_shell'))==credits)
            check('reward_retry.'+event['stage'],ok,True,next_claim is None)
            probes.append({'stage':event['stage'],'passed':ok,'balance_astrite':astrite,'balance_shell':credits})
    check('reward_retry.stage_coverage',[p['stage'] for p in probes],ids)
    check('ledger.final_conditions',sorted(seen),sorted(rewards))
    check('ledger.total',[astrite,credits],[600,300000])
    counts=Counter(c['status'] for c in checks)
    return {'created_at':datetime.now(timezone.utc).isoformat(),'status':'fail' if counts['fail'] else 'missing' if counts['missing'] else 'pass',
            'counts':dict(counts),'checks':checks,'tutorials':tutorials,'training':training,'reward_ledger':ledger,
            'duplicate_claim_probes':probes,'event_sources':list(paths),'sources':list(evidence.sources.values()),
            'scope':'actual continuous editor execution only; automation acknowledgements do not prove human understanding or packaged-game behavior'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reports',nargs='+',type=Path)
    parser.add_argument('--campaign',type=Path,default=ROOT/'Data/Campaign/Campaign.json')
    parser.add_argument('--out-dir',type=Path,default=ROOT/'Saved/Validation/Campaign/continuous-session')
    args=parser.parse_args();campaign=json.loads(args.campaign.read_text(encoding='utf-8'));evidence=Evidence()
    for report in args.reports:evidence.load(report)
    evidence.mark_isolated_tests();result=audit(campaign,evidence)
    result['campaign']={'path':str(args.campaign.resolve()),'sha256':digest(args.campaign)}
    result['input_errors']=evidence.errors
    if evidence.errors and result['status']=='pass':result['status']='missing'
    args.out_dir.mkdir(parents=True,exist_ok=True)
    (args.out_dir/'SessionAudit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    lines=['# 连续九关会话审计','',f"状态：**{result['status']}**；{result['counts']}。",'',
           '| 教学节点 | 状态 | 展示时刻 | 确认时刻 |', '| --- | --- | --- | --- |']
    for row in result['tutorials']:lines.append(f"| {row['id']} | {row['status']} | {[e['sim_seconds'] for e in row['show']]} | {[e['sim_seconds'] for e in row['ack']]} |")
    lines+=['','## 非通过项','']
    for c in result['checks']:
        if c['status']!='pass':lines.append(f"- {c['status']} `{c['id']}`：实际 `{c['actual']}`；预期 `{c['expected']}`。")
    lines+=['','## 真实训练','',json.dumps(result['training'],ensure_ascii=False),'','## 奖励逐次到账','',
            '| 关卡 | 星声增量 | 贝币增量 | 星声余额 | 贝币余额 |','| --- | ---: | ---: | ---: | ---: |']
    for row in result['reward_ledger']:lines.append(f"| {row['stage']} | {row['astrite_delta']} | {row['shell_delta']} | {row['balance_astrite']} | {row['balance_shell']} |")
    lines+=['','自动确认日志只证明节点流程已执行，不代表真人理解测试完成；本报告也不代表新发行包已验收。']
    (args.out_dir/'SessionAudit.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps({'status':result['status'],'counts':result['counts'],'output':str(args.out_dir)},ensure_ascii=False))
    return 1 if result['status']=='fail' else 2 if result['status']=='missing' else 0


if __name__=='__main__':raise SystemExit(main())
