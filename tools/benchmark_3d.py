#!/usr/bin/env python3
"""Prepare a recorded E00 configuration, or compare three alternating A/B pairs."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_new(path, value):
    with path.open('x') as f:
        json.dump(value, f, indent=2, allow_nan=False)
        f.write('\n')


def prepare(args):
    root = args.root.resolve()
    build = json.loads(args.build_options.read_text())
    for key in ('modules', 'compiler', 'optimization', 'debug', 'dmaRef', 'bootDevice', 'image'):
        if key not in build:
            raise ValueError('build options missing ' + key)
    hashes = {name: sha(root / path) for name, path in {
        'harness': 'bin/world_systems_bench.js', 'bench': 'src/modules/bench/js/bench.js',
        'moduleSelection': 'Makefile.modules',
    }.items()}
    hashes['elf'] = sha(args.elf)
    # Includes uncommitted and untracked module sources, unlike git HEAD alone.
    sources = hashlib.sha256()
    for path in sorted((root / 'src').rglob('*')):
        if path.is_file() and path.suffix in ('.c', '.h', '.js', '.vcl', '.vsm', '.json'):
            sources.update(path.relative_to(root).as_posix().encode() + b'\0' + path.read_bytes() + b'\0')
    hashes['sources'] = sources.hexdigest()
    loading = {}
    for name, path in {'model': 'models/lit_cube.glb', 'scene': 'models/arm.glb', 'texture': 'models/checker.png'}.items():
        file = root / 'bin' / path
        loading[name] = {'path': path, 'bytes': file.stat().st_size, 'sha256': sha(file)}
    hashes['assets'] = {v['path']: v['sha256'] for v in loading.values()}
    revision = args.revision or subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    config = {
        'path': args.result, 'warmup': 30, 'samples': 120, 'frameBudgetMs': args.frame_budget,
        'metadata': {'platform': args.platform, 'revision': revision, 'runtime': 'QuickJS',
                     'buildOptions': build, 'hashes': hashes, 'runId': args.run_id,
                     'variant': args.variant, 'pair': args.pair, 'order': args.order,
                     'provenance': 'ELF supplied by operator; source hashes describe --root at preparation time'},
        'loading': loading,
    }
    write_new(args.output, config)
    return config


def stats(values):
    ordered = sorted(values)
    return {'averageMs': sum(values) / len(values), 'p95Ms': ordered[math.ceil(len(values) * .95) - 1],
            'minMs': ordered[0], 'maxMs': ordered[-1]}


def check_samples(result, samples):
    if not isinstance(samples, list) or not samples or any(
            not isinstance(v, (int, float)) or isinstance(v, bool) or not math.isfinite(v) or v < 0 for v in samples):
        raise ValueError('missing or invalid raw samples')
    for name, value in stats(samples).items():
        # Whole-frame summaries need not publish minMs.
        if name == 'minMs' and name not in result:
            continue
        # Athena QuickJS uses float32 arithmetic; sequential sums accumulate
        # up to n unit-roundoff errors. Its short numeric JSON formatting also
        # rounds some summaries to six significant digits. Raw data is kept,
        # and comparisons below recompute means in the host's double precision.
        tolerance = 5e-6 + ((len(samples) + 2) * 2**-24 if name == 'averageMs' else 0)
        if not math.isclose(value, result[name], rel_tol=tolerance, abs_tol=1e-6):
            raise ValueError('summary does not match raw samples: ' + name)


def compare(reports):
    if len(reports) < 6 or len(reports) % 2:
        raise ValueError('at least three complete A/B pairs are required')
    reference = None
    identities, variants, pairs = set(), {}, []
    for index, report in enumerate(reports):
        if report.get('version') != 2 or report.get('status') != 'completed':
            raise ValueError('all reports must be completed Bench v2 reports')
        if not isinstance(report['frameBudgetMs'], (int, float)) or not math.isfinite(report['frameBudgetMs']) or report['frameBudgetMs'] <= 0:
            raise ValueError('invalid frame budget')
        meta = report['metadata']
        pair = index // 2 + 1
        expected = ('before', 'after') if pair % 2 else ('after', 'before')
        if meta['pair'] != pair or meta['order'] != index + 1 or meta['variant'] != expected[index % 2]:
            raise ValueError('reports must follow chronological AB, BA, AB order with numbered pairs')
        if not meta['runId'] or meta['runId'] in identities:
            raise ValueError('runId must be unique')
        identities.add(meta['runId'])
        hashes = meta['hashes']
        for name in ('elf', 'sources', 'moduleSelection', 'harness', 'bench'):
            value = hashes[name]
            if not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
                raise ValueError('invalid SHA-256: ' + name)
        variant_id = [meta['revision'], hashes['elf'], hashes['sources']]
        if meta['variant'] in variants and variants[meta['variant']] != variant_id:
            raise ValueError('build changed between repeats of ' + meta['variant'])
        variants[meta['variant']] = variant_id
        signature = [report['label'], report['frameBudgetMs'], meta['platform'], meta['runtime'],
                     meta['buildOptions'], meta['videoMode'], hashes['harness'], hashes['bench'],
                     hashes['moduleSelection'], hashes['assets'],
                     [(r['name'], r['warmup'], r['samples'], r['iterations'], r['metadata']) for r in report['results']]]
        if not report['results']:
            raise ValueError('empty benchmark')
        if reference is None:
            reference = signature
        elif reference != signature:
            raise ValueError('platform, build options, scene, harness, assets or sampling differ')
        if len({r['name'] for r in report['results']}) != len(report['results']):
            raise ValueError('duplicate task names')
        for result in report['results']:
            if result['status'] != 'completed' or result['samples'] != len(result['sampleMs']):
                raise ValueError('incomplete task')
            if result['samples'] != len(result['sampleTicks']) or result['samples'] != len(result['batchCpu']['sampleMs']):
                raise ValueError('raw sample count mismatch')
            if result['iterations'] <= 0 or result['samples'] <= 0:
                raise ValueError('invalid task sampling')
            check_samples(result, result['sampleMs'])
            check_samples(result['batchCpu'], result['batchCpu']['sampleMs'])
            if any(isinstance(t, bool) or not isinstance(t, int) or not 0 <= t <= 0xffffffff for t in result['sampleTicks']):
                raise ValueError('invalid raw clock ticks')
            if any(b + 1e-6 < v * result['iterations'] for b, v in zip(result['batchCpu']['sampleMs'], result['sampleMs'])):
                raise ValueError('batch CPU time is shorter than work')
            if result['batchCpu']['overBudget'] != sum(v > report['frameBudgetMs'] for v in result['batchCpu']['sampleMs']):
                raise ValueError('invalid budget counter')
        for kind in ('cpu', 'wall'):
            frame = report['frames'][kind]
            check_samples(frame, frame['sampleMs'])
            if len(frame['sampleMs']) != sum(r['warmup'] + r['samples'] for r in report['results']):
                raise ValueError('incomplete total-frame samples')
            if frame['overBudget'] != sum(v > report['frameBudgetMs'] for v in frame['sampleMs']):
                raise ValueError('invalid frame budget counter')
        if index % 2:
            a, b = reports[index - 1:index + 1]
            before, after = (a, b) if a['metadata']['variant'] == 'before' else (b, a)
            pairs.append({'pair': pair, 'order': list(expected),
                          'before': before, 'after': after,
                          'summaryFromSamples': {variant: {
                              r['name']: {'work': stats(r['sampleMs']), 'batchCpu': stats(r['batchCpu']['sampleMs'])}
                              for r in run['results']}
                              for variant, run in (('before', before), ('after', after))},
                          'deltaMs': {r['name']: {key: stats(after['results'][i]['sampleMs'])[key] - stats(r['sampleMs'])[key]
                                                for key in ('averageMs', 'p95Ms', 'maxMs')}
                                      for i, r in enumerate(before['results'])}})
    return {'version': 1, 'scope': 'paired CPU measurements; all runs retained; no hardware or visual acceptance implied',
            'pairs': pairs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    prep = subs.add_parser('prepare')
    prep.add_argument('--root', type=Path, default=ROOT, help='isolated checkout used for the supplied ELF')
    prep.add_argument('--elf', type=Path, required=True)
    prep.add_argument('--build-options', type=Path, required=True, help='JSON with exact recorded build options')
    prep.add_argument('--platform', required=True)
    prep.add_argument('--revision', help='explicit base commit for snapshots without .git; source hashes still record dirty contents')
    prep.add_argument('--run-id', required=True)
    prep.add_argument('--variant', choices=('before', 'after'), required=True)
    prep.add_argument('--pair', type=int, required=True)
    prep.add_argument('--order', type=int, required=True)
    prep.add_argument('--frame-budget', type=float, default=1000 / 60)
    prep.add_argument('--result', required=True, help='writable device path for the resulting report')
    prep.add_argument('--output', type=Path, required=True, help='new config file; refuses overwrite')
    comp = subs.add_parser('compare')
    comp.add_argument('reports', nargs='+', type=Path, help='raw reports in chronological order')
    comp.add_argument('--output', type=Path, required=True, help='new comparison file; refuses overwrite')
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            if args.pair < 1 or args.order < 1 or not math.isfinite(args.frame_budget) or args.frame_budget <= 0:
                raise ValueError('pair/order/budget must be positive')
            prepare(args)
        else:
            result = compare([json.loads(p.read_text()) for p in args.reports])
            result['inputs'] = [{'path': str(p), 'sha256': sha(p)} for p in args.reports]
            write_new(args.output, result)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as e:
        parser.exit(1, str(e) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
