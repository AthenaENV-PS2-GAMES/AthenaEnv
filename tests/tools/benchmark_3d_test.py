"""Synthetic reports test E00 validation; these are not performance evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('benchmark_3d', Path(__file__).resolve().parents[2] / 'tools/benchmark_3d.py')
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def reports():
    result = []
    for index, variant in enumerate(('before', 'after', 'after', 'before', 'before', 'after')):
        samples = [3, 1, 2] if variant == 'before' else [2, 1, 1]
        summary = {**bench.stats(samples), 'sampleMs': samples}
        frame_samples = samples + [samples[1]]
        frame_summary = {**bench.stats(frame_samples), 'sampleMs': frame_samples,
                         'overBudget': sum(v > 2 for v in frame_samples)}
        result.append({'version': 2, 'label': 'test', 'status': 'completed', 'frameBudgetMs': 2,
                       'metadata': {'platform': 'synthetic', 'runtime': 'QuickJS', 'revision': variant,
                                    'buildOptions': {'debug': True}, 'videoMode': {'width': 640},
                                    'hashes': {**{key: 'a' * 64 for key in ('sources', 'moduleSelection', 'harness', 'bench')},
                                               'elf': ('b' if variant == 'before' else 'c') * 64, 'assets': {}},
                                    'runId': str(index), 'variant': variant, 'pair': index // 2 + 1, 'order': index + 1},
                       'results': [{'name': 'work', 'status': 'completed', 'warmup': 1, 'samples': 3,
                                    'iterations': 1, 'metadata': {'gsWait': False}, **summary,
                                    'sampleTicks': [v * 1000 for v in samples],
                                    'batchCpu': {**summary, 'overBudget': sum(v > 2 for v in samples)}}],
                       'frames': {'cpu': frame_summary, 'wall': frame_summary}})
    return result


class ComparisonTests(unittest.TestCase):
    def test_float32_sum_and_short_serialization(self):
        samples = [20.9230986] * 1833
        summary = bench.stats(samples)
        summary['averageMs'] = 20.9223
        summary['p95Ms'] = summary['maxMs'] = summary['minMs'] = 20.9231
        bench.check_samples(summary, samples)
        summary['averageMs'] = 20.9
        with self.assertRaises(ValueError):
            bench.check_samples(summary, samples)

    def test_prepare_hashes_fixtures_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('bin/world_systems_bench.js', 'src/modules/bench/js/bench.js', 'Makefile.modules',
                         'bin/models/lit_cube.glb', 'bin/models/arm.glb', 'bin/models/checker.png', 'test.elf'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'synthetic fixture')
            build_options = root / 'build.json'
            build_options.write_text(json.dumps({k: 'synthetic' for k in (
                'modules', 'compiler', 'optimization', 'debug', 'dmaRef', 'bootDevice', 'image')}))
            args = SimpleNamespace(root=root, elf=root / 'test.elf', build_options=build_options,
                                   revision='synthetic', result='result.json', frame_budget=16,
                                   platform='synthetic', run_id='test', variant='before', pair=1, order=1,
                                   output=root / 'config.json')
            config = bench.prepare(args)
            self.assertEqual(config['metadata']['hashes']['elf'], bench.sha(args.elf))
            self.assertEqual(config['loading']['model']['bytes'], 17)
            self.assertEqual(json.loads(args.output.read_text()), config)
            with self.assertRaises(FileExistsError):
                bench.prepare(args)

    def test_all_runs_retained_and_ba_pair_oriented(self):
        source = reports()
        result = bench.compare(source)
        self.assertEqual(len(result['pairs']), 3)
        self.assertEqual(result['pairs'][1]['before']['metadata']['order'], 4)
        self.assertEqual(result['pairs'][1]['deltaMs']['work']['maxMs'], -1)
        self.assertEqual(result['pairs'][0]['before']['results'][0]['sampleMs'], [3, 1, 2])

    def test_insufficient_pairs(self):
        for n in (1, 2, 4, 5):
            with self.subTest(n=n), self.assertRaises(ValueError):
                bench.compare(reports()[:n])

    def test_incomparable_runs_rejected(self):
        mutations = [
            lambda r: r.update(status='error'),
            lambda r: r.update(version=1),
            lambda r: r['metadata'].update(platform='PS2'),
            lambda r: r['metadata'].update(order=4),
            lambda r: r['metadata'].update(runId='0'),
            lambda r: r['metadata']['buildOptions'].update(debug=False),
            lambda r: r['metadata']['hashes'].update(harness='b' * 64),
            lambda r: r['metadata']['hashes'].update(elf='broken'),
            lambda r: r['metadata']['hashes'].update(sources='b' * 64),
            lambda r: r['results'][0].update(samples=4),
            lambda r: r['results'][0].update(averageMs=0),
            lambda r: r['results'][0]['metadata'].update(gsWait=True),
            lambda r: r['results'][0]['sampleMs'].append(float('nan')),
            lambda r: r['results'][0]['batchCpu'].update(overBudget=100),
            lambda r: r['results'][0]['sampleTicks'].pop(),
            lambda r: r['frames']['cpu'].update(sampleMs=[]),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                source = copy.deepcopy(reports())
                mutate(source[2])
                with self.assertRaises(ValueError):
                    bench.compare(source)


if __name__ == '__main__':
    unittest.main()
