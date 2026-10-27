"""Check the actual scene region of captured PNGs; never alter source images."""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    args=parser.parse_args()
    data=json.loads((args.archive/'scene-checks.json').read_text(encoding='utf-8-sig'))
    rows=[]
    for snapshot in data['snapshots']:
        path=args.archive/Path(snapshot['screenshot']).name
        with Image.open(path) as source:
            im=source.convert('RGB');width,height=im.size;pixels=im.load()
            # Exclude the header, sidebars, bottom cards and notice banner.
            x0,x1=int(width*.30),int(width*.70);y0,y1=int(height*.30),int(height*.73)
            lit=0;total=(x1-x0)*(y1-y0);brightness=0
            for y in range(y0,y1):
                for x in range(x0,x1):
                    rgb=pixels[x,y];value=max(rgb);lit+=value>12;brightness+=sum(rgb)/3
        fraction=lit/total
        rows.append({'label':snapshot['label'],'file':str(path.resolve()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'size':[width,height],'scene_roi':[x0,y0,x1,y1],'nonblack_fraction':fraction,
            'mean_rgb':brightness/total,'passed':fraction>.5})
    result={'status':'pass' if rows and all(r['passed'] for r in rows) else 'fail',
        'criterion':'For this lit courtyard, more than half of the central world region must be above RGB max 12. UI is excluded. Complements mesh/render/collision checks; not a general artistic-quality score.',
        'images':rows,'failures':sum(not r['passed'] for r in rows)}
    (args.archive/'scene-image-checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'status':result['status'],'images':len(rows),'failures':result['failures'],
        'min_nonblack_fraction':min(r['nonblack_fraction'] for r in rows),'archive':str(args.archive)},ensure_ascii=False))
    return 0 if result['status']=='pass' else 1

if __name__=='__main__':raise SystemExit(main())
