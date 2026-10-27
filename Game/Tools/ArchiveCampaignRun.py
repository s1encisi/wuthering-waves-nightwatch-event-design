"""Freeze one explicit nine-stage run and its actual event stream before another run."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',required=True)
    parser.add_argument('--destination',required=True,type=Path)
    args=parser.parse_args()
    campaign=ROOT/'Data/Campaign/Campaign.json'
    definition=json.loads(campaign.read_text(encoding='utf-8'))
    paths=[ROOT/'Saved/Telemetry/Campaign'/f'{stage["id"]}-all-v0-{args.plan}.json' for stage in definition['stages']]
    reports=[json.loads(path.read_text(encoding='utf-8')) for path in paths]
    streams={row['events'] for row in reports}
    if len(streams)!=1 or any(row['mode']!='all' or row['formation_plan']!=args.plan or row['result']!='VICTORY' for row in reports):
        raise RuntimeError('Reports do not identify one successful continuous run')
    destination=args.destination.resolve()
    destination.mkdir(parents=True,exist_ok=False)
    observed=destination/'observed-inputs';observed.mkdir()
    sources=paths+[Path(next(iter(streams))),campaign,ROOT/'Data/Campaign/ValidationPlans.json']
    records=[]
    for source in sources:
        target=observed/source.name;shutil.copy2(source,target)
        sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
        if sha(source)!=sha(target):raise RuntimeError('Copy checksum mismatch: '+str(source))
        records.append({'source':str(source),'archive':str(target),'sha256':sha(target),'byte_identical':True})
    (observed/'manifest.json').write_text(json.dumps({'files':records,'scope':'one continuous editor run; raw JSON and event TSV unchanged'},ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'archive':str(destination),'reports':len(paths),'events':next(iter(streams)),
        'spawned':sum(r['spawned'] for r in reports),'killed':sum(r['killed'] for r in reports),'leaked':sum(r['leaked'] for r in reports),
        'defense':{r['stage']:r['defense'] for r in reports}},ensure_ascii=False))

if __name__=='__main__':main()
