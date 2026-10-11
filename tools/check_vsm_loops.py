#!/usr/bin/env python3
"""Flags VF registers that an OpenVCL-generated .vsm loop reads before writing
them in the same iteration and also writes later in that iteration. Such a
register either carries a value from the previous iteration or clobbers a
value set before the loop: OpenVCL does not keep values live across the
loop's back edge (render3d keeps the state carried between vertices in VU
memory instead). Also flags an upper and a lower instruction writing the same
VF register in one cycle (VU1 keeps only the upper result).
Usage: tools/check_vsm_loops.py file.vsm... (exit status 1 on findings)."""
import re, sys

WRITE_FIRST = re.compile(r'^(?!sq|isw|clip|ibne|ibeq|b\b|nop|waitq|fcand|fmand|fcset|mtir|ilw|iadd|iaddi|iaddiu|ior|iand|xtop|xitop|xgkick|div|loi|sqrt|rsqrt)([a-z0-9.]+)\s+(VF\d+)')

def fields(slot):
    m = re.match(r'([a-z0-9.]+)\s*(.*)', slot.strip())
    if not m:
        return None, []
    return m.group(1), [o.strip() for o in m.group(2).split(',') if o.strip()]

def regs(text):
    return set(re.findall(r'VF(\d+)', text)) - {'00'}

def check(path):
    lines = open(path).read().splitlines()
    problems = []
    loops = [(i, l[:-1]) for i, l in enumerate(lines) if re.match(r'^\w+:$', l)]
    for start, label in loops:
        ends = [j for j in range(start + 1, len(lines)) if re.search(rf'\bib\w*\s.*\b{label}\b', lines[j])]
        if not ends:
            continue
        end = ends[-1]
        written, read_first, written_later = set(), set(), set()
        for line in lines[start + 1:end + 1]:
            slots = [s for s in re.split(r'\s{2,}', line.strip()) if s]
            wrote_this_cycle = []
            for slot in slots:
                op, ops = fields(slot)
                if not op or op == 'nop':
                    continue
                dest = ops[0] if ops and WRITE_FIRST.match(slot) else None
                sources = ops[1:] if dest else ops
                # Stores read their first operand.
                for src in sources:
                    for r in regs(src):
                        if r not in written:
                            read_first.add(r)
                if dest:
                    # An accumulator (add VF14, VF14, ...) keeps its value in
                    # place: carried on purpose, in the same register.
                    accumulates = regs(dest) & set().union(*[regs(s) for s in sources]) if sources else set()
                    for r in regs(dest):
                        if r in read_first and r not in accumulates:
                            written_later.add(r)
                        written.add(r)
                        wrote_this_cycle.append(r)
            dup = {r for r in wrote_this_cycle if wrote_this_cycle.count(r) > 1}
            for r in dup:
                problems.append(f'{path}: VF{r} written by both slots: {line.strip()}')
        for r in sorted(written_later):
            problems.append(f'{path}: loop {label}: VF{r} is read before being written in the iteration and written later')
    return problems

if __name__ == '__main__':
    found = [p for f in sys.argv[1:] for p in check(f)]
    print('\n'.join(found) if found else f'vsm loops: {len(sys.argv) - 1} files clean')
    sys.exit(1 if found else 0)
