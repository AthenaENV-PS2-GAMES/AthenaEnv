#!/usr/bin/env python3
"""Run the installed PCSX2 Flatpak with isolated data and stop after an artifact/marker.

The data path must contain PCSX2/inis/PCSX2.ini (PCSX2 appends that subdir).
Only the process group created by this invocation is stopped. No settings,
BIOS or memory cards in the operator's default profile are modified.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--share-root', type=Path, required=True)
    parser.add_argument('--data-path', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--artifact', type=Path, help='Bench report, complete with frames')
    parser.add_argument('--marker', help='success marker in the emulation log')
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--snapshot-marker', help='capture render window after this log marker')
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--x11', action='store_true', help='use Qt X11 for window-scoped captures')
    args = parser.parse_args()
    if not args.artifact and not args.marker:
        parser.error('--artifact or --marker is required')
    shared = args.share_root.resolve()
    for path in (args.data_path, args.elf, args.log, args.artifact, args.snapshot):
        if path and not path.resolve().is_relative_to(shared):
            parser.error('all paths must be inside --share-root')
    if not (args.data_path/'PCSX2/inis/PCSX2.ini').is_file():
        parser.error('isolated PCSX2/inis/PCSX2.ini is missing')
    if args.log.exists() or (args.artifact and args.artifact.exists()):
        parser.error('refusing to reuse existing evidence')
    command = ['flatpak', 'run', '--filesystem='+str(shared)]
    if args.x11:
        command += ['--nosocket=wayland', '--socket=x11', '--env=QT_QPA_PLATFORM=xcb',
                    '--env=DISPLAY='+os.environ.get('DISPLAY', ':0')]
    command += ['net.pcsx2.PCSX2',
               '-batch', '-nogui', '-datapath', str(args.data_path.resolve()),
               '-elf', str(args.elf.resolve()), '-logfile', str(args.log.resolve())]
    deadline = time.monotonic() + args.timeout
    snapshot_at = None
    saved = False
    snapshot_error = None
    outcome = 'timeout'
    with args.log.with_suffix('.console.log').open('x') as console:
        process = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while time.monotonic() < deadline:
                log = args.log.read_text(errors='replace') if args.log.exists() else ''
                if args.snapshot_marker and args.snapshot_marker in log and snapshot_at is None:
                    snapshot_at = time.monotonic() + .5
                if snapshot_at and not saved and time.monotonic() >= snapshot_at:
                    snapshot_at = None
                    args.snapshot_marker = None
                    try:
                        from PIL import ImageGrab
                        windows = subprocess.check_output(['xdotool', 'search', '--onlyvisible', '--class', '(pcsx2|PCSX2)'], text=True).split()
                        if windows:
                            geometry = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', windows[-1]], text=True)
                            pos = dict(line.split('=', 1) for line in geometry.splitlines() if '=' in line)
                            x, y, w, h = (int(pos[k]) for k in ('X', 'Y', 'WIDTH', 'HEIGHT'))
                            ImageGrab.grab(bbox=(x, y, x+w, y+h)).save(args.snapshot)
                            saved = True
                    except (OSError, subprocess.CalledProcessError) as error:
                        snapshot_error = str(error)
                if args.artifact and args.artifact.exists():
                    try:
                        report = json.loads(args.artifact.read_text())
                    except (OSError, ValueError):
                        report = {}
                    if report.get('frames') and report.get('status') != 'running':
                        outcome = 'passed' if report.get('status') == 'completed' else 'report-error'
                        break
                if args.marker and args.marker in log:
                    outcome = 'passed'
                    break
                if 'Uncaught ' in log:
                    outcome = 'uncaught-error'
                    break
                if process.poll() is not None:
                    outcome = 'exited-without-evidence'
                    break
                time.sleep(.25)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    manifest = {'command': command, 'outcome': outcome, 'exit_code': process.returncode,
                'termination': 'own process group stopped after evidence or timeout; guest Loop.stop does not exit PCSX2',
                'snapshot': str(args.snapshot) if saved else None, 'snapshot_error': snapshot_error}
    args.log.with_suffix('.launch.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(outcome + ': ' + str(args.log), flush=True)
    return 0 if outcome == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
