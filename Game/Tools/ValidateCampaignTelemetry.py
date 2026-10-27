"""Independently verify real campaign reports/events against Campaign.json.

No engine/source-code inspection can produce a runtime pass. Exit codes: 0 all
required checks pass, 1 contradictory evidence, 2 required evidence is missing.
Inputs are stage report objects, JSON lists of reports/events, or engine TSVs.
"""
from __future__ import annotations
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fields(event):
    result = dict(event)
    detail = event.get('detail', '')
    if isinstance(detail, dict):
        result.update(detail)
    elif isinstance(detail, str):
        result.update({k: v.strip() for k, v in re.findall(r'(\w+)\s*=\s*([^;]*)', detail)})
    return result


def integer(value):
    try:
        number = float(value)
        return int(number) if number.is_integer() else None
    except (TypeError, ValueError):
        return None


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def truth(value):
    return str(value).lower() in ('1', 'true', 'yes')


def resolve_event_path(value, report_path):
    path = Path(value)
    direct = path.resolve() if path.is_absolute() else (Path(report_path).parent / path).resolve()
    if direct.is_file() or path.is_absolute():
        return direct, 'absolute' if path.is_absolute() else 'relative_to_report'
    # UE FPaths may serialize a path relative to its executable working folder.
    # Resolve the exact observed project/Saved suffix, never a filename search.
    parts = path.parts
    for index, component in enumerate(parts[:-1]):
        if component.lower() == ROOT.name.lower() and parts[index+1].lower() == 'saved':
            candidate = ROOT.joinpath(*parts[index+1:]).resolve()
            if candidate.is_relative_to(ROOT) and candidate.is_file():
                return candidate, 'explicit_project_saved_suffix_from_engine_path'
    return direct, 'unresolved_relative_to_report'


class Evidence:
    def __init__(self):
        self.reports, self.events, self.sources, self.errors = [], [], {}, []

    def load(self, path):
        path = Path(path).resolve()
        if str(path) in self.sources:
            return
        if not path.is_file():
            self.errors.append({'source': str(path), 'error': 'missing file'})
            return
        try:
            raw=path.read_bytes()
            self.sources[str(path)] = {'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}
            if path.suffix.lower() == '.tsv':
                # Engine protocol uses LF records and strips tabs from detail.
                # Strategy JSON detail may retain bare CR; it is data within the
                # fifth column, not a new record. Preserve original bytes/hash.
                lines=raw.decode('utf-8-sig').split('\n')
                header=lines[0].removesuffix('\r').split('\t')
                if header!=['stage','wave','sim_seconds','event','detail']:
                    raise ValueError('Unexpected engine TSV header')
                self.sources[str(path)]['embedded_carriage_returns']=len(re.findall(b'\r(?!\n)',raw))
                rows=[]
                for line_number,line in enumerate(lines[1:],2):
                    if not line.strip():continue
                    columns=line.removesuffix('\r').split('\t',4)
                    if len(columns)!=5:raise ValueError(f'TSV record {line_number} does not contain five columns')
                    rows.append(dict(zip(header,columns)))
                self.add(rows,path)
            else:
                self.add(json.loads(raw.decode('utf-8-sig')), path)
        except (ValueError, OSError, TypeError) as error:
            self.errors.append({'source': str(path), 'error': str(error)})

    def add(self, payload, path):
        if isinstance(payload, list):
            for item in payload:
                self.add(item, path)
            return
        if not isinstance(payload, dict):
            raise ValueError('Expected JSON object or list')
        if 'reports' in payload:
            self.add(payload['reports'], path)
            if isinstance(payload.get('events'), list):
                self.add(payload['events'], path)
            return
        if 'event' in payload:
            event = fields(payload)
            event['_source'] = str(path)
            event['_index'] = len(self.events)
            self.events.append(event)
            return
        if 'stage' in payload and ('waves' in payload or 'result' in payload):
            report = dict(payload)
            report['_source'] = str(path)
            self.reports.append(report)
            if isinstance(payload.get('events'), str):
                events, resolution = resolve_event_path(payload['events'], path)
                report['_events_path'] = str(events)
                report['_event_path_resolution'] = resolution
                self.load(events)
            elif isinstance(payload.get('events'), list):
                for item in payload['events']:
                    self.add(dict(item, stage=item.get('stage', payload['stage'])), path)
            return
        raise ValueError('Object is neither a stage report nor an event')

    def derive_tsv_reports(self):
        """Use explicit completion events if only TSV/event-list was supplied."""
        represented = {(r['stage'], r['_source']) for r in self.reports}
        for report in self.reports:
            if isinstance(report.get('events'), str):
                path = report.get('_events_path', str(resolve_event_path(report['events'], report['_source'])[0]))
                represented.add((report['stage'], path))
        for event in self.events:
            key = (event.get('stage'), event['_source'])
            if event['event'] != 'stage_complete' or key in represented or event.get('_isolated'):
                continue
            selected = [e for e in self.events if (e.get('stage'), e['_source']) == key and not e.get('_isolated')]
            waves = [dict(e, seconds=e.get('sim_seconds')) for e in selected if e['event'] == 'wave_complete']
            report = dict(event, result='VICTORY', waves=waves, events=event['_source'], mode='play', variant=event.get('variant', 0))
            report['_events_path'] = event['_source']
            self.reports.append(report)

    def mark_isolated_tests(self):
        active = set()
        for event in self.events:
            source = event['_source']
            detail = str(event.get('detail', ''))
            if event['event'] == 'state_test' and re.search(r'(?:^|;)\s*begin(?:;|$)', detail):
                active.add(source)
            event['_isolated'] = source in active or event['event'] == 'state_test'
            if event['event'] == 'state_test' and re.search(r'(?:^|;)\s*end(?:;|$)', detail):
                active.discard(source)
        for source in active:
            self.errors.append({'source': source, 'error': 'unterminated isolated state_test scope'})


class Validation:
    def __init__(self, campaign, evidence, stage_ids, variant=0):
        self.campaign, self.evidence = campaign, evidence
        self.stage_ids, self.variant = stage_ids, variant
        self.checks, self.selected = [], []

    def check(self, key, condition, expected=None, actual=None, evidence=None, missing=False):
        self.checks.append({'id': key, 'status': 'missing' if missing else 'pass' if condition else 'fail',
                            'expected': expected, 'actual': actual, 'evidence': evidence})

    def wave(self, stage, wave, report, events, previous_defense, assistance=None):
        prefix = f"{stage['id']}.wave{wave['index']}"
        expected = sum(batch['count'] for batch in wave['batches'])
        source = report.get('_source')
        spawned, killed, leaked = (integer(report.get(key)) for key in ('spawned', 'killed', 'leaked'))
        self.check(prefix + '.enemy_count', spawned == expected, expected, spawned, source, spawned is None)
        self.check(prefix + '.conservation', None not in (spawned, killed, leaked) and spawned == killed + leaked,
                   'spawned = killed + leaked, all nonnegative', [spawned, killed, leaked], source,
                   None in (spawned, killed, leaked))
        if None not in (spawned, killed, leaked):
            self.check(prefix + '.nonnegative', min(spawned, killed, leaked) >= 0, '>= 0', [spawned, killed, leaked], source)
        defense = integer(report.get('defense'))
        self.check(prefix + '.defense_alive', defense is not None and 0 < defense <= previous_defense,
                   f'0 < defense <= {previous_defense}', defense, source, defense is None)
        tasks = integer(report.get('tasks'))
        fraction = report.get('task_fraction')
        self.check(prefix + '.task_count', tasks == len(wave['tasks']), len(wave['tasks']), tasks, source, tasks is None)
        self.check(prefix + '.task_completion', number(fraction) is not None and number(fraction) >= .99999,
                   1, fraction, source, fraction is None)
        for key, expected_value in [('planned_enemies', expected), ('living_enemies', 0),
                                    ('completed_batches', len(wave['batches']))]:
            actual = integer(report.get(key))
            self.check(prefix + '.' + key, actual == expected_value, expected_value, actual, source, actual is None)
        for key in ('all_batches_spawned', 'complete'):
            self.check(prefix + '.' + key, truth(report.get(key)), True, report.get(key), source, key not in report)
        # Engine serial/batch/entry indices are zero-based except enemy serials.
        # Normalize only fields explicitly present in observed events.
        normalized = []
        for original in events:
            event = dict(original)
            if 'id' not in event and 'enemy_id' not in event and integer(event.get('enemy')) is not None:
                event['id'] = event['enemy']
            if 'kind' in event:
                event['enemy'] = event['kind']
            if 'batch_id' not in event and integer(event.get('batch')) is not None:
                index = integer(event['batch'])
                if 0 <= index < len(wave['batches']):
                    event['batch_id'] = wave['batches'][index]['id']
            if 'entry_id' in event:
                event['entry'] = event['entry_id']
            elif integer(event.get('entry')) is not None:
                index = integer(event['entry'])
                if 0 <= index < len(stage['entries']):
                    event['entry'] = stage['entries'][index]['id']
            normalized.append(event)
        events = normalized
        spawns = [e for e in events if e['event'] in ('spawn', 'enemy_spawn', 'enemy_spawned')]
        terminals = [e for e in events if e['event'] in ('enemy_killed', 'enemy_leaked', 'enemy_defeated', 'breach', 'kill', 'leak')]
        ids = [str(e.get('id', e.get('enemy_id', ''))) for e in spawns]
        complete_ids = bool(spawns) and all(ids)
        self.check(prefix + '.unique_spawn_ids', len(ids) == len(set(ids)), expected,
                   {'spawn_events': len(spawns), 'unique_ids': len(set(ids))},
                   sorted({e['_source'] for e in spawns}), not complete_ids)
        self.check(prefix + '.spawn_event_count', len(spawns) == expected, expected, len(spawns), source, not spawns)
        batches = Counter(str(e.get('batch_id', e.get('batch', ''))) for e in spawns)
        expected_batches = {b['id']: b['count'] for b in wave['batches']}
        batch_known = bool(spawns) and '' not in batches
        self.check(prefix + '.batch_counts', dict(batches) == expected_batches, expected_batches, dict(batches), source, not batch_known)
        planned = {b['id']: b for b in wave['batches']}
        malformed = []
        missing_identity = False
        for event in spawns:
            batch = planned.get(str(event.get('batch_id', event.get('batch', ''))))
            if batch is None:
                continue
            if 'enemy' not in event or 'entry' not in event:
                missing_identity = True
            elif event['enemy'] != batch['enemy'] or event['entry'] != batch['entry']:
                malformed.append({k: event.get(k) for k in ('id', 'batch_id', 'enemy', 'entry')})
        self.check(prefix + '.spawn_identity', not malformed, 'authored enemy and entry per batch', malformed,
                   source, not batch_known or missing_identity)
        terminal_ids = [str(e.get('id', e.get('enemy_id', ''))) for e in terminals]
        terminals_known = bool(terminals) and all(terminal_ids) and complete_ids
        self.check(prefix + '.terminal_ids', Counter(terminal_ids) == Counter(ids) and len(terminal_ids) == len(set(terminal_ids)),
                   'every spawned ID ends exactly once', {'spawn': len(ids), 'terminal': len(terminal_ids),
                   'missing': sorted(set(ids) - set(terminal_ids)), 'unknown': sorted(set(terminal_ids) - set(ids))},
                   source, not terminals_known)
        kills = [e for e in terminals if e['event'] in ('enemy_killed', 'enemy_defeated', 'kill')]
        leaks = [e for e in terminals if e['event'] in ('enemy_leaked', 'breach', 'leak')]
        self.check(prefix + '.terminal_totals', len(kills) == killed and len(leaks) == leaked,
                   {'killed': killed, 'leaked': leaked}, {'killed': len(kills), 'leaked': len(leaks)}, source, not terminals_known)
        losses = [integer(e.get('loss')) for e in leaks]
        losses_known = terminals_known and None not in losses
        self.check(prefix + '.defense_accounting', losses_known and defense == max(0, previous_defense - sum(losses)),
                   previous_defense - sum(losses) if losses_known else 'previous defense - observed leak losses',
                   defense, source, not losses_known)
        spawn_by_id = {str(e.get('id', e.get('enemy_id', ''))): e for e in spawns}
        bad_losses, loss_unknown = [], False
        for event in leaks:
            spawn = spawn_by_id.get(str(event.get('id', event.get('enemy_id', ''))), {})
            definition = self.campaign['enemies'].get(spawn.get('enemy'))
            if not definition or assistance is None or integer(event.get('loss')) is None:
                loss_unknown = True
                continue
            expected_loss = definition['breach_loss'] * (self.campaign['rules']['assistance_breach_multiplier'] if assistance else 1)
            if integer(event['loss']) != expected_loss:
                bad_losses.append({'id': event.get('id'), 'expected': expected_loss, 'actual': event['loss']})
        self.check(prefix + '.authored_breach_loss', not bad_losses, 'configured enemy loss, halved only under assistance',
                   bad_losses, source, loss_unknown or not terminals_known)
        completed = [str(e.get('task_id', e.get('task', e.get('id', '')))) for e in events if e['event'] == 'task_complete']
        expected_tasks = [t['id'] for t in wave['tasks']]
        self.check(prefix + '.task_ids', Counter(completed) == Counter(expected_tasks), expected_tasks, completed,
                   source, bool(expected_tasks) and (not completed or '' in completed))
        return defense if defense is not None else previous_defense

    def run(self):
        for index, error in enumerate(self.evidence.errors):
            self.check(f'input.{index}', False, 'readable evidence', error['error'], error['source'], error['error'] == 'missing file')
        stages = {stage['id']: stage for stage in self.campaign['stages']}
        for sid in self.stage_ids:
            stage = stages[sid]
            candidates = [r for r in self.evidence.reports if r.get('stage') == sid
                          and integer(r.get('variant', 0)) == self.variant
                          and r.get('mode', 'play') in ('all', 'play', 'render')]
            self.check(sid + '.report', len(candidates) == 1, 'one unambiguous full-stage runtime report',
                       len(candidates), [r['_source'] for r in candidates], not candidates)
            if len(candidates) != 1:
                continue
            report = candidates[0]
            self.selected.append(report)
            source = report['_source']
            for key, expected in [('enemy_health_scale', 1), ('batch_time_offset', 0)]:
                value = number(report.get(key))
                self.check(sid + '.nominal_' + key, value == expected, expected, value, source, value is None)
            raw_events = report.get('events')
            event_source = report.get('_events_path', str(resolve_event_path(raw_events, source)[0])) if isinstance(raw_events, str) else raw_events
            events = [e for e in self.evidence.events if e.get('stage') == sid
                      and (not isinstance(raw_events, str) or e['_source'] == event_source)
                      and not truth(e.get('training', False)) and not e.get('_isolated')]
            self.check(sid + '.victory', report.get('result') == 'VICTORY', 'VICTORY', report.get('result'), source)
            wave_reports = report.get('waves', [])
            actual_wave_ids = [integer(w.get('wave')) for w in wave_reports]
            expected_wave_ids = list(range(1, len(stage['waves']) + 1))
            self.check(sid + '.wave_sequence', actual_wave_ids == expected_wave_ids, expected_wave_ids, actual_wave_ids, source)
            previous = self.campaign['rules']['defense_initial']
            for i, wave in enumerate(stage['waves'], 1):
                matches = [w for w in wave_reports if integer(w.get('wave')) == i]
                if len(matches) != 1:
                    self.check(f'{sid}.wave{i}.report', False, 'one wave report', len(matches), source, not matches)
                    continue
                wr = dict(matches[0], _source=source)
                we = [e for e in events if integer(e.get('wave')) == i]
                previous = self.wave(stage, dict(wave, index=i), wr, we, previous,
                                     truth(report['assistance']) if 'assistance' in report else None)
            expected_total = sum(sum(b['count'] for b in w['batches']) for w in stage['waves'])
            totals = {key: integer(report.get(key)) for key in ('spawned', 'killed', 'leaked')}
            self.check(sid + '.total_enemies', totals['spawned'] == expected_total, expected_total, totals['spawned'], source, totals['spawned'] is None)
            for key, value in totals.items():
                numbers = [integer(w.get(key)) for w in wave_reports]
                self.check(sid + '.wave_sum_' + key, value == sum(n for n in numbers if n is not None),
                           sum(n for n in numbers if n is not None), value, source, value is None or None in numbers)
            self.check(sid + '.final_defense', integer(report.get('defense')) == previous, previous,
                       integer(report.get('defense')), source, 'defense' not in report)
            injected = [e['event'] for e in events if e['event'] in ('state_injection', 'test_state_injection', 'debug_mutation', 'force_complete', 'state_probe')]
            self.check(sid + '.no_state_injection', not injected, [], injected, source, not events)
        self.reward_checks()
        counts = Counter(c['status'] for c in self.checks)
        status = 'fail' if counts['fail'] else 'missing' if counts['missing'] else 'pass'
        return {'schema_version': 1, 'generated_at': datetime.now(timezone.utc).isoformat(),
                'status': status, 'counts': dict(counts), 'required_stages': self.stage_ids,
                'variant': self.variant, 'checks': self.checks,
                'sources': list(self.evidence.sources.values()), 'input_errors': self.evidence.errors,
                'excluded_isolated_events': sum(bool(e.get('_isolated')) for e in self.evidence.events),
                'event_path_resolutions': [{'stage': r['stage'], 'raw': r.get('events'), 'resolved': r.get('_events_path'),
                                            'method': r.get('_event_path_resolution')} for r in self.selected if isinstance(r.get('events'), str)],
                'scope': 'actual stage/wave population, enemy conservation, unique spawns, defense, tasks and rewards; no claim about human comprehension or visual quality'}

    def reward_checks(self):
        rewards = {r['id']: r for r in self.campaign['rewards']}
        selected_paths = set()
        for report in self.selected:
            value = report.get('events')
            if isinstance(value, str):
                selected_paths.add(report.get('_events_path', str(resolve_event_path(value, report['_source'])[0])))
            else:
                selected_paths.add(report['_source'])
        events = [e for e in self.evidence.events if e['_source'] in selected_paths and e.get('stage') in self.stage_ids
                  and e['event'] == 'reward_claim' and not e.get('_isolated')]
        observed = []
        for index, event in enumerate(events):
            raw = event.get('conditions', event.get('reward_ids'))
            if raw is None:
                self.check(f'rewards.event{index}.conditions', False, 'claimed reward condition IDs', None, event['_source'], True)
                continue
            ids = raw if isinstance(raw, list) else [x.strip() for x in str(raw).split(',') if x.strip()]
            observed.extend(ids)
            known = all(rid in rewards for rid in ids)
            self.check(f'rewards.event{index}.known', known, 'Campaign.json reward IDs', ids, event['_source'])
            if known:
                for key, aliases in [('astrite', ('astrite_delta', 'astrites_delta')), ('shell_credits', ('shell_delta', 'shell_credits_delta'))]:
                    value = next((integer(event[a]) for a in aliases if a in event), None)
                    expected = sum(rewards[rid][key] for rid in ids)
                    self.check(f'rewards.event{index}.{key}', value == expected, expected, value, event['_source'], value is None)
        expected_ids = [r['id'] for r in rewards.values() if r.get('stage_id') in self.stage_ids
                        or r.get('kind') == 'all_main_complete' and set(r['stage_ids']).issubset(self.stage_ids)]
        missing_conditions=set(expected_ids)-set(observed)
        contradictory=bool(set(observed)-set(expected_ids)) or len(observed)!=len(set(observed))
        self.check('rewards.conditions_coverage', Counter(observed) == Counter(expected_ids), expected_ids, observed,
                   sorted(selected_paths), bool(missing_conditions) and not contradictory)
        self.check('rewards.no_duplicate_claims', len(observed) == len(set(observed)), 'each condition once',
                   dict(Counter(observed)), sorted(selected_paths), not observed)


def write_report(report, output):
    output.mkdir(parents=True, exist_ok=True)
    (output / 'CampaignTelemetry.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    lines = ['# 九关运行证据独立校验', '', f"总体状态：**{report['status']}**。", '',
             f"检查计数：`{json.dumps(report['counts'], ensure_ascii=False)}`。",
             '', '本报告仅依据输入运行报告和事件日志。缺少证据标为 missing；配置存在或源码包含实现不构成通过。', '',
             '| 检查 | 状态 | 预期 | 实际 |', '| --- | --- | --- | --- |']
    for check in report['checks']:
        clean = lambda value: json.dumps(value, ensure_ascii=False).replace('|', '\\|').replace('\n', ' ')
        lines.append(f"| {check['id']} | {check['status']} | {clean(check['expected'])} | {clean(check['actual'])} |")
    lines += ['', '## 输入证据', '']
    lines += [f"- `{item['path']}`，SHA-256 `{item['sha256']}`。" for item in report['sources']]
    if report['input_errors']:
        lines += ['', '## 无法读取的证据', ''] + [f"- `{e['source']}`：{e['error']}" for e in report['input_errors']]
    (output / 'CampaignTelemetry.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', nargs='*', type=Path, help='Report JSON/list or TSV; default Saved/Telemetry/Campaign/*-all-v0.json')
    parser.add_argument('--campaign', type=Path, default=ROOT / 'Data/Campaign/Campaign.json')
    parser.add_argument('--out-dir', type=Path, default=ROOT / 'Saved/Validation/Campaign')
    parser.add_argument('--stages', nargs='+', help='Limit scope explicitly; default all nine stages')
    parser.add_argument('--variant', type=int, default=0)
    args = parser.parse_args()
    campaign = json.loads(args.campaign.read_text(encoding='utf-8-sig'))
    stage_ids = args.stages or [stage['id'] for stage in campaign['stages']]
    if set(stage_ids) - {s['id'] for s in campaign['stages']}:
        parser.error('Unknown required stage ID')
    evidence = Evidence()
    paths = args.evidence or sorted((ROOT / 'Saved/Telemetry/Campaign').glob('*-all-v0.json'))
    for path in paths:
        evidence.load(path)
    evidence.mark_isolated_tests()
    evidence.derive_tsv_reports()
    result = Validation(campaign, evidence, stage_ids, args.variant).run()
    result['campaign'] = {'path': str(args.campaign.resolve()), 'sha256': digest(args.campaign)}
    write_report(result, args.out_dir)
    print(json.dumps({'status': result['status'], 'counts': result['counts'], 'output': str(args.out_dir)}, ensure_ascii=False))
    return 1 if result['status'] == 'fail' else 2 if result['status'] == 'missing' else 0


if __name__ == '__main__':
    raise SystemExit(main())
