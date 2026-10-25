# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# The three MCUs of the sample on one virtual bus, what scripts/run.sh does for
# twister: twister builds a, b and c (sample.yaml) and starts a as the DUT; this
# test starts b and c, waits for the verdict of every MCU, and checks that the
# requests sent and served add up.

import re
import subprocess
import time
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter

BUS_SHM = '/dev/shm/zephyr_i2c_mm_0'  # bus-id 0, boards/native_sim.overlay
VERDICT = re.compile(r'all settings verified|settings check failed')
STATS = re.compile(r'sent (\d+) requests, served (\d+) requests')
TIMEOUT_S = 30


def bus_users() -> list[int]:
    """PIDs of the processes that have the bus mapped"""
    if not Path(BUS_SHM).exists():
        return []  # the last process to leave removes it
    pids = []
    for maps in Path('/proc').glob('[0-9]*/maps'):
        try:
            if BUS_SHM in maps.read_text():
                pids.append(int(maps.parent.name))
        except OSError:
            pass  # gone, or not ours
    return pids


class Peer:
    """An MCU besides the DUT: its own process, flash and log"""

    def __init__(self, build_dir: Path, seed: int):
        self.name = build_dir.name
        self.log = build_dir / 'peer.log'
        with self.log.open('w') as out:
            self.proc = subprocess.Popen(
                [
                    build_dir / 'zephyr' / 'zephyr.exe',
                    f'--seed={seed}',
                    f'--flash={build_dir / "flash.bin"}',
                ],
                cwd=build_dir,
                stdout=out,
                stderr=subprocess.STDOUT,
            )

    def output(self) -> str:
        return self.log.read_text(errors='replace')

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()


@pytest.fixture
def peers(required_build_dirs: list[str]):
    users = bus_users()
    assert not users, f'processes left over on the bus, kill them first: {users}'

    started = []
    try:
        for i, build_dir in enumerate(sorted(required_build_dirs)):
            started.append(Peer(Path(build_dir), seed=2 + i))
        yield started
    finally:
        for peer in started:
            peer.stop()


def test_settings(peers: list[Peer], unlaunched_dut: DeviceAdapter):
    dut = unlaunched_dut
    dut.launch()  # after the peers: all three join the bus within ~1 s anyway

    # the statistics come just before the verdict
    dut_lines = dut.readlines_until(regex=VERDICT.pattern, timeout=TIMEOUT_S)

    deadline = time.monotonic() + TIMEOUT_S
    while not all(VERDICT.search(p.output()) for p in peers):
        assert time.monotonic() < deadline, 'a peer gave no verdict'
        time.sleep(0.2)

    outputs = {'dut': '\n'.join(dut_lines)} | {p.name: p.output() for p in peers}
    for name, out in outputs.items():
        if 'all settings verified' not in out:
            pytest.fail(f'{name} failed:\n{out}')

    sent = served = 0
    for name, out in outputs.items():
        m = STATS.search(out)
        assert m, f'{name}: no statistics'
        sent += int(m[1])
        served += int(m[2])
    assert sent == served, f'{sent} requests sent, {served} served'
    assert sent > 0
