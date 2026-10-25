# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# The shells of the boards a, b and c, over the bus: twister builds the four
# MCUs (sample.yaml) and starts the gateway as the DUT; this test starts the
# boards, opens the PTY the gateway has for each, and runs shell commands there.
# A command that gets a setting of another board goes over the bus once more;
# the fs commands work on the board's littlefs. Every board runs in its build
# folder, so its flash is the flash.bin there. MCUmgr (SMP) on the same PTY
# moves a file to and from /lfs, and an image into slot 1.

import asyncio
import os
import re
import select
import subprocess
import time
import tty
from pathlib import Path

import pytest
from smpclient import SMPClient
from smpclient.mcuboot import IMAGE_TLV, ImageInfo
from smpclient.requests.image_management import ImageStatesRead
from smpclient.transport.serial import SMPSerialTransport
from twister_harness import DeviceAdapter

BUS_SHM = '/dev/shm/zephyr_i2c_mm_1'  # bus-id 1, boards/native_sim.overlay
BOARDS = {'a': 0x10, 'b': 0x11, 'c': 0x12}
PTY = re.compile(r'pty-(\w) connected to pseudotty: (\S+)')
TIMEOUT_S = 10


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


class Shell:
    """The shell of a board, on its PTY of the gateway"""

    def __init__(self, path: str, name: str):
        self.name = name
        self.prompt = f'{name}:~$ '
        self.buf = b''
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)

    def close(self):
        os.close(self.fd)

    def read_until(self, text: str, timeout: float = TIMEOUT_S) -> str:
        """Output up to and including `text`; what came after it is kept for the next read"""
        end = time.monotonic() + timeout
        while (i := self.buf.find(text.encode())) < 0:
            left = end - time.monotonic()
            assert left > 0, f'{self.name}: no {text!r} in {self.buf.decode(errors="replace")!r}'
            if select.select([self.fd], [], [], left)[0]:
                self.buf += os.read(self.fd, 4096)
        i += len(text)
        out, self.buf = self.buf[:i], self.buf[i:]
        return out.decode(errors='replace')

    def run(self, command: str) -> str:
        """Output of `command`, up to the next prompt"""
        os.write(self.fd, command.encode() + b'\r')
        self.read_until(command + '\r\n')  # the echo
        return self.read_until(self.prompt)[: -len(self.prompt)]


@pytest.fixture
def boards(required_build_dirs: list[str]):
    users = bus_users()
    assert not users, f'processes left over on the bus, kill them first: {users}'

    procs = []
    try:
        for build_dir in map(Path, required_build_dirs):
            with (build_dir / 'board.log').open('w') as out:
                procs.append(
                    subprocess.Popen(
                        [build_dir / 'zephyr' / 'zephyr.exe'],
                        cwd=build_dir,
                        stdout=out,
                        stderr=subprocess.STDOUT,
                    )
                )
        yield procs
    finally:
        for proc in procs:
            proc.terminate()
        for proc in procs:
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


@pytest.fixture
def ptys(boards, unlaunched_dut: DeviceAdapter) -> dict[str, str]:
    """The gateway's PTY of every board"""
    dut = unlaunched_dut
    dut.launch()
    lines = dut.readlines_until(regex=r'pty-c connected', timeout=TIMEOUT_S)
    ptys = dict(m.groups() for m in map(PTY.search, lines) if m)
    assert set(ptys) == set(BOARDS), f'PTYs of the gateway: {ptys}'
    return ptys


@pytest.fixture
def shells(ptys: dict[str, str]):
    opened = {n: Shell(ptys[n], f'mcu-{n}@{BOARDS[n]:x}') for n in BOARDS}
    try:
        for shell in opened.values():
            # A board that started before the gateway lost its first prompt
            os.write(shell.fd, b'\r')
            shell.read_until(shell.prompt)
        yield opened
    finally:
        for shell in opened.values():
            shell.close()


def test_shell(shells: dict[str, Shell]):
    names = list(BOARDS)
    for i, n in enumerate(names):
        shell = shells[n]
        other = names[(i + 1) % len(names)]
        addr = BOARDS[other]

        out = shell.run(f'mcubus ping 0x{addr:02x}')
        assert f'0x{addr:02x}: protocol 1, messages up to 64 bytes' in out, out

        out = shell.run(f'mcubus get 0x{addr:02x} mcu/name')
        assert f'0x{addr:02x} mcu/name = "mcu-{other}@{addr:x}"' in out

        out = shell.run(f'mcubus set 0x{addr:02x} mcu/name x')
        assert f'mcu/name: {-13}' in out  # -EACCES: read-only

        # The board's own littlefs, on its own flash
        out = shell.run(f'fs write /lfs/{n} -o 0 {" ".join(f"{b:x}" for b in n.encode())}')
        assert 'Failed' not in out, out
        out = shell.run('fs ls /lfs')
        assert re.search(rf'^\s*1 {n}\r?$', out, re.M), out
        out = shell.run(f'fs cat /lfs/{n}')
        assert out.strip() == n, out

        # Many chunks in a row, more than the gateway's buffers
        out = shell.run('help')
        assert 'mcubus' in out and len(out) > 512, out


async def smp_transfers(pty: str, image_path: Path):
    """A file to /lfs and back, then an image into slot 1"""
    async with SMPClient(SMPSerialTransport(), pty, timeout_s=TIMEOUT_S) as client:
        data = os.urandom(1024)
        async for _ in client.upload_file(data, '/lfs/smp.bin'):
            pass
        assert await client.download_file('/lfs/smp.bin') == data

        async for _ in client.upload(image_path.read_bytes()):
            pass
        states = await client.request(ImageStatesRead())

    (slot1,) = [img for img in states.images if img.slot == 1]
    image = ImageInfo.load_file(str(image_path))  # its pydantic model takes no Path
    assert slot1.version == '1.2.3', slot1
    assert slot1.hash == image.get_tlv(IMAGE_TLV.SHA256).value, slot1


def test_smp(ptys: dict[str, str], tmp_path: Path):
    # An MCUboot image of random bytes, as "west sign" makes it of a build, for
    # native_sim's slot size (boards/native/native_sim/native_sim.dts)
    payload, image_path = tmp_path / 'payload.bin', tmp_path / 'image.bin'
    payload.write_bytes(os.urandom(1024))
    subprocess.run(
        ['imgtool', 'create', '--version', '1.2.3', '--header-size', '0x200', '--pad-header',
         '--slot-size', '0x69000', payload, image_path],
        check=True,
    )  # fmt: skip
    asyncio.run(smp_transfers(ptys['a'], image_path))
