#!/usr/bin/env python3
"""Validate/summarize all three alternating pairs from nav_incremental_bench.js.

Same-ELF API comparison; keeps input evidence and per-round distributions.
No physical-console acceptance or percentage speedup is inferred.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
from benchmark_3d import stats, write_new


def summarize(report):
    if report.get('version') != 1 or report.get('status') != 'completed' or report.get('label') != 'nav-incremental':
        raise ValueError('expected a completed Nav incremental report')
    meta = report['metadata']
    if meta['warmup'] != 3 or meta['samples'] != 30 or len(report['rounds']) != 6:
        raise ValueError('expected six rounds of 3 warmup and 30 measured queries')
    expected = ['sync', 'incremental', 'incremental', 'sync', 'sync', 'incremental']
    summaries = []
    for index, r in enumerate(report['rounds']):
        if r['mode'] != expected[index] or r['pair'] != index // 2 + 1 or r['order'] != index % 2 + 1:
            raise ValueError('expected alternating chronological pairs')
        raw, count = r['raw'], r['frames']
        if not isinstance(count, int) or count <= 0 or set(raw) != {'ticks', 'work', 'cpuMs', 'wallMs', 'query', 'phaseBefore', 'phaseAfter', 'final'}:
            raise ValueError('invalid raw frames')
        for key, values in raw.items():
            if len(values) != count or any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in values):
                raise ValueError('invalid samples: '+key)
            if key not in ('cpuMs', 'wallMs') and any(not isinstance(v, int) for v in values):
                raise ValueError('noninteger raw counter')
        if len(r['queries']) != 33 or any(t > 0xffffffff for t in raw['ticks']):
            raise ValueError('missing queries or clock overflow')
        offset = 0
        for qi, q in enumerate(r['queries']):
            end = offset + q['frames']
            if q['index'] != qi or q['warmup'] != (qi < 3) or q['first'] != offset or not 0 < q['frames'] <= count:
                raise ValueError('invalid query boundaries')
            if q['pointCount'] != len(meta['reference']) // 2 or q['expanded'] != meta['referenceExpanded']:
                raise ValueError('path or expansion differs from reference')
            if raw['query'][offset:end] != [qi]*q['frames'] or raw['final'][offset:end] != [0]*(q['frames']-1)+[1]:
                raise ValueError('incomplete final frame')
            if sum(raw['ticks'][offset:end]) != q['totalTicks'] or sum(raw['work'][offset:end]) != q['totalWork']:
                raise ValueError('query totals mismatch')
            if r['mode'] == 'incremental':
                if not q['expanded'] < q['totalWork'] or not q['storageBytes'] >= 4096 or not q['cost'] > 0:
                    raise ValueError('missing reconstruction/memory/cost counters')
                if any(not 1 <= w <= meta['maxWork'] for w in raw['work'][offset:end]):
                    raise ValueError('work budget violation')
            elif q['frames'] != 1:
                raise ValueError('synchronous query must occupy one frame')
            offset = end
        if offset != count or any(p > 8 for p in raw['phaseBefore']+raw['phaseAfter']):
            raise ValueError('incomplete frames/phases')
        measured = [i for i, q in enumerate(raw['query']) if q >= 3]
        final = [i for i in measured if raw['final'][i]]
        # EE COP0 Count runs at 294912 ticks/ms. All raw samples retained.
        step = [raw['ticks'][i]/294912 for i in measured]
        cpu = [raw['cpuMs'][i] for i in measured]
        summaries.append({'pair': r['pair'], 'order': r['order'], 'mode': r['mode'],
            'stepMs': stats(step), 'finalStepMs': stats([raw['ticks'][i]/294912 for i in final]),
            'frameCpuMs': stats(cpu), 'frameWallMs': stats([raw['wallMs'][i] for i in measured]),
            'queryCpuMs': stats([q['totalTicks']/294912 for q in r['queries'][3:]]),
            'queryFrameCpuMs': stats([sum(raw['cpuMs'][q['first']:q['first']+q['frames']]) for q in r['queries'][3:]]),
            'queryFrameWallMs': stats([sum(raw['wallMs'][q['first']:q['first']+q['frames']]) for q in r['queries'][3:]]),
            'latencyFrames': {k.removesuffix('Ms'): v for k, v in stats([q['frames'] for q in r['queries'][3:]]).items()},
            'measuredFrames': len(measured), 'stepOver2Ms': sum(v > 2 for v in step),
            'stepOverBudgetMs': sum(v > meta['budgetMs'] for v in step) if r['mode'] == 'incremental' else None,
            'maxBudgetOvershootMs': max(0, max(step)-meta['budgetMs']) if r['mode'] == 'incremental' else None,
            'frameCpuOver2Ms': sum(v > 2 for v in cpu),
            'frameCpuOver16_667Ms': sum(v > 1000/60 for v in cpu),
            'workUnits': sorted({q['totalWork'] for q in r['queries'][3:]}),
            'storageBytes': sorted({q['storageBytes'] for q in r['queries'][3:]})})
    if report['frames'] != sum(r['frames'] for r in report['rounds']):
        raise ValueError('total frame count mismatch')
    pairs = []
    for i in range(0, 6, 2):
        before, after = summaries[i:i+2] if expected[i] == 'sync' else summaries[i:i+2][::-1]
        pairs.append({'pair': i//2+1, 'queryCpuDeltaMs': after['queryCpuMs']['averageMs']-before['queryCpuMs']['averageMs'],
                      'stepP95DeltaMs': after['stepMs']['p95Ms']-before['stepMs']['p95Ms'],
                      'scope': 'total query CPU and per-call CPU are different metrics; no total speedup implied'})
    return {'version': 1, 'scope': 'same-ELF API comparison on PCSX2; all six rounds retained in input',
            'rounds': summaries, 'pairs': pairs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        value = summarize(json.loads(args.report.read_text()))
        value['input'] = {'path': str(args.report), 'sha256': hashlib.sha256(args.report.read_bytes()).hexdigest()}
        write_new(args.output, value)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, str(error)+'\n')
    print(args.output)


if __name__ == '__main__':
    main()
