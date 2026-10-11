#!/usr/bin/env python3
"""Validate every frame/completion in the repeated Crowd FIFO stress benchmark."""
import argparse
import hashlib
import json
import math
from pathlib import Path
from benchmark_3d import stats, write_new


def summarize(report):
    if report.get('version') != 1 or report.get('label') != 'nav-crowd-queue' or report.get('status') != 'completed':
        raise ValueError('expected completed Crowd queue report')
    meta = report['metadata']; agents = meta['agents']
    if not isinstance(agents, int) or not 1 <= agents <= 16 or meta['warmup'] != 1 or meta['samples'] != 3 or len(report['bursts']) != 4:
        raise ValueError('invalid burst configuration')
    summaries = []
    for index, burst in enumerate(report['bursts']):
        raw, count = burst['raw'], burst['frames']
        if burst['index'] != index or burst['warmup'] != (index == 0) or not isinstance(count, int) or count < agents:
            raise ValueError('invalid burst boundaries')
        if set(raw) != {'ticks', 'cpuMs', 'wallMs', 'work', 'completed', 'pending'}:
            raise ValueError('missing raw arrays')
        for key, values in raw.items():
            if len(values) != count or any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in values):
                raise ValueError('invalid raw array: '+key)
            if key not in ('cpuMs', 'wallMs') and any(not isinstance(v, int) for v in values):
                raise ValueError('counter must be integer')
        if any(not 1 <= w <= meta['maxWork'] for w in raw['work']) or any(t > 0xffffffff for t in raw['ticks']):
            raise ValueError('work budget or tick range invalid')
        if sum(raw['work']) != agents * meta['referenceWorkUnits']:
            raise ValueError('work differs from identical standalone reference queries')
        latency = burst['latency']
        if len(latency) != agents:
            raise ValueError('missing completion evidence')
        prev = -1
        for i, l in enumerate(latency):
            end = l['completed']
            if l['id'] != i or l['enqueued'] != i or not isinstance(end, int) or not i <= end < count or end < prev or l['frames'] != end-i+1:
                raise ValueError('invalid FIFO completion or latency')
            prev = end
        if latency[-1]['completed'] != count-1:
            raise ValueError('missing final completion frame')
        for frame in range(count):
            complete = sum(l['completed'] <= frame for l in latency)
            if raw['completed'][frame] != complete or raw['pending'][frame] != min(frame+1, agents)-complete:
                raise ValueError('queue count mismatch')
        calls = [t/294912 for t in raw['ticks']]
        # A step may complete more than one request. Retain distinct call indices.
        final = sorted({l['completed'] for l in latency})
        summaries.append({'index': index, 'warmup': index == 0, 'frames': count,
            'stepMs': stats(calls), 'completionStepMs': stats([calls[i] for i in final]),
            'frameCpuMs': stats(raw['cpuMs']), 'frameWallMs': stats(raw['wallMs']),
            'frameCpuOver2Ms': sum(v > 2 for v in raw['cpuMs']),
            'frameCpuOver16_667Ms': sum(v > 1000/60 for v in raw['cpuMs']),
            'maxBudgetOvershootMs': max(0, max(calls)-meta['budgetMs']),
            'peakPending': max(raw['pending']), 'totalWork': sum(raw['work']),
            'totalStepCpuMs': sum(calls), 'totalFrameCpuMs': sum(raw['cpuMs']),
            'latency': latency})
    if report['frames'] != sum(b['frames'] for b in report['bursts']):
        raise ValueError('total frame count mismatch')
    return {'version': 1, 'scope': 'one warmup and three FIFO stress repeats; no before/after speedup claimed', 'bursts': summaries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path); parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = summarize(json.loads(args.report.read_text()))
        result['input'] = {'path': str(args.report), 'sha256': hashlib.sha256(args.report.read_bytes()).hexdigest()}
        write_new(args.output, result)
    except (OSError, ValueError, TypeError, KeyError) as e:
        parser.exit(1, str(e)+'\n')
    print(args.output)


if __name__ == '__main__':
    main()
