#!/usr/bin/env python3
"""Measure the monitor, not Codex. Uses disposable synthetic fixtures only."""
import argparse
import json
import os
from pathlib import Path
import sqlite3
import statistics
import subprocess
import tempfile
import time


def metrics(pid):
    base = Path('/proc') / str(pid)
    stat = (base / 'stat').read_text().rsplit(')', 1)[1].split()
    memory = {}
    for line in (base / 'smaps_rollup').read_text().splitlines():
        key, _, value = line.partition(':')
        fields = value.split()
        if len(fields) == 2 and fields[1] == 'kB':
            memory[key] = int(fields[0]) * 1024
    return {
        'cpu_seconds': (int(stat[11]) + int(stat[12])) / os.sysconf('SC_CLK_TCK'),
        'rss_bytes': int(stat[21]) * os.sysconf('SC_PAGE_SIZE'),
        'pss_bytes': memory.get('Pss', 0),
        'uss_bytes': sum(memory.get(k, 0) for k in ('Private_Clean', 'Private_Dirty', 'Private_Hugetlb')),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/codex-killer')
    parser.add_argument('--platform', default='offscreen', choices=['offscreen', 'xcb', 'wayland'])
    parser.add_argument('--seconds', type=float, default=12)
    parser.add_argument('--hosts', type=int, default=0)
    parser.add_argument('--fixture', default='build/fixtures/codex')
    args = parser.parse_args()
    if args.seconds < 2 or args.hosts < 0 or args.hosts > 100:
        parser.error('seconds must be >= 2; hosts must be between 0 and 100')
    children = []
    with tempfile.TemporaryDirectory(prefix='codex-killer-bench-') as home:
        env = dict(os.environ, CODEX_HOME=home, QT_QPA_PLATFORM=args.platform)
        env.pop('CODEX_THREAD_ID', None)
        try:
            db = sqlite3.connect(Path(home) / 'state_5.sqlite')
            db.execute('CREATE TABLE threads(id TEXT PRIMARY KEY,title TEXT,model TEXT)')
            db.execute('CREATE TABLE thread_spawn_edges(parent_thread_id TEXT,child_thread_id TEXT,status TEXT)')
            for n in range(args.hosts):
                thread_id = f'00000000-0000-0000-0000-{n:012d}'
                rollout = Path(home) / 'sessions/2026/01/01' / f'rollout-benchmark-{thread_id}.jsonl'
                rollout.parent.mkdir(parents=True, exist_ok=True)
                rollout.write_text(json.dumps({'type': 'session_meta', 'payload': {'id': thread_id}}) + '\n')
                db.execute('INSERT INTO threads VALUES(?,?,?)', (thread_id, 'Synthetic benchmark task', 'example-model'))
                child = subprocess.Popen([args.fixture, '--rollout', str(rollout)], env=env,
                                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                children.append(child)
                if child.stdout.readline() != b'ready\n':
                    raise RuntimeError('Fixture did not start')
            db.commit()
            db.close()
            monitor = subprocess.Popen([args.binary, '--quit-after-ms', str(int((args.seconds + 10) * 1000))],
                                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            children.append(monitor)
            time.sleep(2)  # Warm up font/toolkit caches and the first sample.
            start = time.monotonic()
            baseline = metrics(monitor.pid)
            samples = []
            while time.monotonic() - start < args.seconds:
                time.sleep(0.5)
                samples.append(metrics(monitor.pid))
            elapsed = time.monotonic() - start
            result = {
                'platform': args.platform,
                'synthetic_hosts': args.hosts,
                'measurement_seconds': round(elapsed, 2),
                'warmup_seconds': 2,
                'cpu_percent_one_core': round((samples[-1]['cpu_seconds'] - baseline['cpu_seconds']) / elapsed * 100, 3),
                'rss_median_mib': round(statistics.median(x['rss_bytes'] for x in samples) / 2**20, 2),
                'rss_peak_mib': round(max(x['rss_bytes'] for x in samples) / 2**20, 2),
                'pss_median_mib': round(statistics.median(x['pss_bytes'] for x in samples) / 2**20, 2),
                'uss_median_mib': round(statistics.median(x['uss_bytes'] for x in samples) / 2**20, 2),
            }
            print(json.dumps(result, indent=2))
        finally:
            # Only children created by this script are cleaned up. Never discover-and-kill.
            for child in reversed(children):
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()


if __name__ == '__main__':
    main()
