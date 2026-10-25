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
import subprocess
import sys
from pathlib import Path

import pytest
from smpclient import SMPClient
from smpclient.mcuboot import IMAGE_TLV, ImageInfo
from smpclient.requests.image_management import ImageStatesRead
from smpclient.transport.serial import SMPSerialTransport
from twister_harness import DeviceAdapter

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'scripts' / 'pylib'))
from mcubus_bench import TIMEOUT_S, Shell, ptys  # noqa: E402
from mcubus_bench import start_nodes as start_boards  # noqa: E402

BUS_ID = 5  # boards/native_sim.overlay
BOARDS = {'a': 0x10, 'b': 0x11, 'c': 0x12}


@pytest.fixture
def ptys_of_boards(required_build_dirs: list[str], unlaunched_dut: DeviceAdapter):
    """The gateway's PTY of every board, with the boards running"""
    with start_boards(required_build_dirs, BUS_ID):
        unlaunched_dut.launch()
        lines = unlaunched_dut.readlines_until(regex=r'pty-c connected', timeout=TIMEOUT_S)
        paths = ptys(lines)
        assert set(paths) == set(BOARDS), f'PTYs of the gateway: {paths}'
        yield paths


@pytest.fixture
def shells(ptys_of_boards: dict[str, str]):
    opened = {n: Shell(ptys_of_boards[n], f'mcu-{n}@{BOARDS[n]:x}:~$ ') for n in BOARDS}
    try:
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


def test_smp(ptys_of_boards: dict[str, str], tmp_path: Path):
    # An MCUboot image of random bytes, as "west sign" makes it of a build, for
    # native_sim's slot size (boards/native/native_sim/native_sim.dts)
    payload, image_path = tmp_path / 'payload.bin', tmp_path / 'image.bin'
    payload.write_bytes(os.urandom(1024))
    subprocess.run(
        ['imgtool', 'create', '--version', '1.2.3', '--header-size', '0x200', '--pad-header',
         '--slot-size', '0x69000', payload, image_path],
        check=True,
    )  # fmt: skip
    asyncio.run(smp_transfers(ptys_of_boards['a'], image_path))
