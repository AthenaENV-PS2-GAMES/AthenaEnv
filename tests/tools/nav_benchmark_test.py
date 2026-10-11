"""Reject corrupted/partial incremental benchmark evidence (synthetic timings)."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from nav_benchmark import summarize


def report():
    meta = {'warmup': 3, 'samples': 30, 'reference': [1, 1, 2, 2], 'referenceExpanded': 4,
            'maxWork': 3, 'budgetMs': 1}
    rounds = []
    for ri, mode in enumerate(['sync', 'incremental', 'incremental', 'sync', 'sync', 'incremental']):
        raw = {k: [] for k in ('ticks', 'work', 'cpuMs', 'wallMs', 'query', 'phaseBefore', 'phaseAfter', 'final')}
        queries = []
        for qi in range(33):
            frames = 1 if mode == 'sync' else 2
            first = len(raw['ticks'])
            for frame in range(frames):
                final = int(frame == frames-1)
                for k, v in {'ticks': 100, 'work': 4 if mode == 'sync' else 3,
                             'cpuMs': 1.5, 'wallMs': 16.7, 'query': qi, 'phaseBefore': 0,
                             'phaseAfter': 8 if final else 3, 'final': final}.items():
                    raw[k].append(v)
            queries.append({'index': qi, 'warmup': qi < 3, 'first': first, 'frames': frames,
                            'totalTicks': frames*100, 'totalWork': 4 if mode == 'sync' else 6,
                            'pointCount': 2, 'expanded': 4, 'cost': 5, 'storageBytes': 4304})
        rounds.append({'pair': ri//2+1, 'order': ri%2+1, 'mode': mode, 'raw': raw,
                       'frames': len(raw['ticks']), 'queries': queries})
    return {'version': 1, 'status': 'completed', 'label': 'nav-incremental', 'metadata': meta,
            'rounds': rounds, 'frames': sum(r['frames'] for r in rounds)}


class NavEvidenceTests(unittest.TestCase):
    def test_all_rounds_final_calls_and_latency(self):
        result = summarize(report())
        self.assertEqual(len(result['rounds']), 6)
        self.assertEqual(result['rounds'][1]['latencyFrames']['p95'], 2)
        self.assertEqual(result['rounds'][1]['measuredFrames'], 60)
        self.assertEqual(result['rounds'][1]['queryFrameCpuMs']['averageMs'], 3)
        self.assertIsNone(result['rounds'][0]['maxBudgetOvershootMs'])
        self.assertGreater(result['pairs'][0]['queryCpuDeltaMs'], 0)

    def test_partial_or_corrupt_evidence(self):
        mutations = [lambda r: r.update(status='running'),
                     lambda r: r['rounds'].pop(),
                     lambda r: r['rounds'][1]['raw']['final'].__setitem__(-1, 0),
                     lambda r: r['rounds'][1]['raw']['cpuMs'].pop(),
                     lambda r: r['rounds'][1]['queries'][3].update(totalTicks=0),
                     lambda r: r['rounds'][1]['raw']['ticks'].__setitem__(0, float('nan')),
                     lambda r: r['metadata'].update(maxWork=1),
                     lambda r: r['rounds'][1]['queries'][3].update(expanded=0),
                     lambda r: r['rounds'][1].update(pair=3)]
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                value = copy.deepcopy(report()); mutation(value); summarize(value)
