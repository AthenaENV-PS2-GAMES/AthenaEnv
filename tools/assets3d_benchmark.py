#!/usr/bin/env python3
"""Compare three alternating E03 pairs with variable frames per asset request."""
import argparse
import json
import math
from pathlib import Path
from benchmark_3d import sha, stats, check_samples, write_new

def compare(reports):
    if len(reports)<6 or len(reports)%2:
        raise ValueError('at least three complete pairs required')
    reference=None; identities=set(); builds={}; pairs=[]
    for i,r in enumerate(reports):
        m=r['metadata']; pair=i//2+1; order=('before','after') if pair%2 else ('after','before')
        if r.get('status')!='completed' or r.get('version')!=2 or r['label'] not in ('assets3d-e03','assets3d-e03-steps','assets3d-e03-final'):
            raise ValueError('completed E03 report required')
        if m['pair']!=pair or m['order']!=i+1 or m['variant']!=order[i%2]:
            raise ValueError('AB/BA/AB order required')
        if not m['runId'] or m['runId'] in identities: raise ValueError('duplicate run ID')
        identities.add(m['runId'])
        h=m['hashes']
        if not isinstance(m['ticksPerMs'],(int,float)) or not math.isfinite(m['ticksPerMs']) or m['ticksPerMs']<=0:
            raise ValueError('invalid clock scale')
        for value in [h[k] for k in ('elf','sources','harness','bench','moduleSelection')]+list(h['assets'].values())+[m['emulatorConfigSha256']]:
            if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value):
                raise ValueError('invalid SHA-256')
        build=(m['revision'],h['elf'],h['sources'])
        if m['variant'] in builds and builds[m['variant']]!=build: raise ValueError('variant build changed')
        builds[m['variant']]=build
        signature=(m['platform'],m['runtime'],m['buildOptions'],m['videoMode'],m['emulatorConfigSha256'],
            h['harness'],h['bench'],h['moduleSelection'],h['assets'],m['workScope'],m['batchScope'],m['frameScope'],
            r['frameBudgetMs'],[(v['name'],v['warmup'],v['samples'],v['iterations'],{k:x for k,x in v['metadata'].items() if k!='phases'}) for v in r['results']])
        if reference is None: reference=signature
        elif reference!=signature: raise ValueError('configuration, fixtures or sampling changed')
        if len(r['results'])<4 or len({v['name'] for v in r['results']})!=len(r['results']): raise ValueError('at least four distinct tasks required')
        expected_frames=0
        for task in r['results']:
            if task['status']!='completed' or task['iterations']!=1: raise ValueError('incomplete task')
            requests=task['metrics']['requests']; count=task['warmup']+task['samples']
            if len(requests)!=count: raise ValueError('request count mismatch')
            for j,q in enumerate(requests):
                if q['warmup']!=(j<task['warmup']) or not isinstance(q['frames'],int) or q['frames']<1:
                    raise ValueError('invalid request/warmup')
                if len(q['progress'])!=q['frames'] or not 0<=q['readingFrames']<=q['frames']:
                    raise ValueError('progress was filtered')
                if any(isinstance(q[k],bool) or not isinstance(q[k],int) or not 0<=q[k]<=0xffffffff for k in ('peakTicks','totalTicks')) or q['peakTicks']>q['totalTicks']:
                    raise ValueError('invalid request clocks')
                expected_frames+=q['frames']+1 # final completion/release frame, also retained
            measured=requests[task['warmup']:]
            if len(task['sampleTicks'])!=task['samples'] or len(task['sampleMs'])!=task['samples'] or len(task['batchCpu']['sampleMs'])!=task['samples']:
                raise ValueError('sample count mismatch')
            if task['sampleTicks']!=[q['peakTicks'] for q in measured]: raise ValueError('peak samples disagree with requests')
            check_samples(task,task['sampleMs']);check_samples(task['batchCpu'],task['batchCpu']['sampleMs'])
            scale=m['ticksPerMs']
            for q,peak,total in zip(measured,task['sampleMs'],task['batchCpu']['sampleMs']):
                if not math.isclose(peak,q['peakTicks']/scale,rel_tol=1e-5,abs_tol=1e-5) or not math.isclose(total,q['totalTicks']/scale,rel_tol=1e-5,abs_tol=1e-5):
                    raise ValueError('clock conversion mismatch')
        for kind in ('cpu','wall'):
            frame=r['frames'][kind];check_samples(frame,frame['sampleMs'])
            if len(frame['sampleMs'])!=expected_frames: raise ValueError('complete frames missing')
            if frame['overBudget']!=sum(v>r['frameBudgetMs'] for v in frame['sampleMs']): raise ValueError('invalid frame excess counter')
        if i%2:
            a,b=reports[i-1:i+1];before,after=(a,b) if a['metadata']['variant']=='before' else (b,a)
            pairs.append({'pair':pair,'order':list(order),'before':before,'after':after,
                'summaryFromSamples':{variant:{task['name']:{'peakUpdate':stats(task['sampleMs']),
                    'sumUpdates':stats(task['batchCpu']['sampleMs'])} for task in run['results']}
                    for variant,run in (('before',before),('after',after))},
                'phases':{variant:{task['name']:task['metadata']['phases'] for task in run['results'] if 'phases' in task['metadata']}
                    for variant,run in (('before',before),('after',after))} if any('phases' in t['metadata'] for t in before['results']) else None})
    return {'version':1,'scope':'Peak synchronous pump span per asset request; total pump CPU and every complete frame retained. No strict parse/upload budget or physical PS2 acceptance implied.', 'pairs':pairs}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('reports',nargs='+',type=Path);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    result=compare([json.loads(p.read_text()) for p in args.reports])
    result['inputs']=[{'path':str(p),'sha256':sha(p)} for p in args.reports]
    write_new(args.output,result)
    print('E03 variable-frame paired comparison passed')
