"""End-to-end CLI checks using only Python's standard library."""
import csv
import math
from pathlib import Path
import subprocess
import sys
import tempfile

executable = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    roots = [Path(directory) / "first", Path(directory) / "second"]
    for root in roots:
        subprocess.run([executable, "--ticks", "1000", "--output", str(root)], check=True)
    for filename in ("summary.csv", "users.csv"):
        assert (roots[0] / filename).read_bytes() == (roots[1] / filename).read_bytes(), "Non-deterministic output"
    with (roots[0] / "summary.csv").open() as handle:
        summary = list(csv.DictReader(handle))
    with (roots[0] / "users.csv").open() as handle:
        users = list(csv.DictReader(handle))
    assert len(summary) == 6 and len(users) == 24
    for row in summary:
        group = [u for u in users if (u['scenario'], u['algorithm']) == (row['scenario'], row['algorithm'])]
        assert len(group) == 4
        total_sent = 0
        for user in group:
            admitted, sent, queued = (int(user[k]) for k in ('admitted_bytes', 'sent_bytes', 'queued_bytes'))
            assert admitted == sent + queued
            total_sent += sent
        assert math.isclose(float(row['throughput_mbps']), total_sent * .008 / 1000, abs_tol=1e-6)
        assert 0 <= float(row['rb_utilization']) <= 1
        assert 0 < float(row['jain_fairness']) <= 1
        assert int(row['completed_packets']) > 0
    equal = [r for r in summary if r['scenario'] == 'equal']
    assert all(float(r['throughput_mbps']) == 7.68 and float(r['jain_fairness']) == 1 for r in equal)
    varying = {r['algorithm']: float(r['throughput_mbps']) for r in summary if r['scenario'] == 'varying_channel'}
    # This known trace has rotating good-channel periods: PF should exploit them.
    assert varying['proportional_fair'] > varying['round_robin']
    for args in (['--ticks', '0'], ['--ticks', '-1'], ['--ticks', 'abc'], ['--ticks', '100001'], ['--ticks'], ['--unknown', '1']):
        result = subprocess.run([executable, *args], capture_output=True, text=True)
        assert result.returncode != 0 and 'Error:' in result.stderr
print('PASS: six experiments, CSV accounting, replay, known baselines and invalid CLI inputs')
