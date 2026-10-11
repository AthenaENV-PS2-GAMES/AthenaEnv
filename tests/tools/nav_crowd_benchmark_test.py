import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from nav_crowd_benchmark import summarize


def report():
    bursts = []
    for i in range(4):
        bursts.append({'index': i, 'warmup': i == 0, 'frames': 10,
            'raw': {'ticks': [100]*10, 'cpuMs': [.5]*10, 'wallMs': [16.7]*10, 'work': [1]*10,
                    'completed': [0]*4+[1]*5+[2], 'pending': [1]+[2]*3+[1]*5+[0]},
            'latency': [{'id': 0, 'enqueued': 0, 'completed': 4, 'frames': 5},
                        {'id': 1, 'enqueued': 1, 'completed': 9, 'frames': 9}]})
    return {'version': 1, 'status': 'completed', 'label': 'nav-crowd-queue', 'frames': 40,
            'metadata': {'agents': 2, 'warmup': 1, 'samples': 3, 'maxWork': 8, 'budgetMs': 1, 'referenceWorkUnits': 5}, 'bursts': bursts}


class CrowdEvidenceTests(unittest.TestCase):
    def test_all_repeats_and_final_calls(self):
        r = summarize(report()); self.assertEqual(len(r['bursts']), 4)
        self.assertEqual(r['bursts'][1]['latency'][1]['frames'], 9)
        self.assertEqual(r['bursts'][1]['peakPending'], 2)

    def test_incomplete_and_corrupt_evidence(self):
        mutations = [lambda r: r.update(status='running'), lambda r: r['bursts'].pop(),
                     lambda r: r['bursts'][1]['latency'][-1].update(completed=8),
                     lambda r: r['bursts'][1]['raw']['cpuMs'].pop(),
                     lambda r: r['bursts'][1]['raw']['pending'].__setitem__(0, 0),
                     lambda r: r['metadata'].update(referenceWorkUnits=6),
                     lambda r: r['bursts'][1]['raw']['work'].__setitem__(0, 0)]
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                r = copy.deepcopy(report()); mutation(r); summarize(r)
