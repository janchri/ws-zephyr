# SPDX-License-Identifier: Apache-2.0
"""Build and serve the workspace guides without running assurance workflows."""

import os
import subprocess
import sys
from pathlib import Path

from west.commands import WestCommand

sys.path.insert(0, str(Path(__file__).resolve().parent))
from docs_artifacts import publish_safety  # noqa: E402


class Docs(WestCommand):
    def __init__(self):
        super().__init__(
            'docs',
            'build and serve workspace documentation',
            'Build and serve the workspace application guides.',
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, help=self.help, description=self.description)
        stages = parser.add_subparsers(dest='stage', required=True)
        stages.add_parser('build', help='build the application guides')
        stages.add_parser('show', help='serve the guides at localhost:8000')
        return parser

    def do_run(self, args, unknown_args):
        if unknown_args:
            self.die('Unrecognized arguments:', ' '.join(unknown_args))
        workspace = Path(self.topdir).resolve()
        repo = Path(__file__).resolve().parent.parent
        build = workspace / 'build/docs-general'
        safety_deploy = workspace / 'build/docs/deploy'
        safety_available = (safety_deploy / 'html/workspace/applications/warmguard.html').is_file()
        temporary = workspace / 'tmp'
        temporary.mkdir(parents=True, exist_ok=True)
        (temporary / 'cache').mkdir(exist_ok=True)
        env = dict(
            os.environ,
            TMPDIR=str(temporary),
            PYTHONPYCACHEPREFIX=str(temporary / 'pycache'),
            XDG_CACHE_HOME=str(temporary / 'cache'),
        )
        if args.stage == 'build':
            commands = [
                [
                    'cmake',
                    '-S',
                    str(repo / 'doc'),
                    '-B',
                    str(build),
                    f'-DZephyr_DIR={workspace}/zephyr/share/zephyr-package/cmake',
                    f'-DZEPHYR_BASE={workspace}/zephyr',
                    f'-DUSER_CACHE_DIR={temporary}/zephyr-cache',
                    '-DZDOCS_TWISTER_OUT=',
                    '-DZDOCS_SPHINXOPTS=-q -j 1 -W --keep-going '
                    '-D affirmatrix_status_enabled=0 '
                    f'-D warmguard_safety_available={int(safety_available)}',
                ],
                ['cmake', '--build', str(build), '--target', 'all-docs', '--parallel', '2'],
            ]
        else:
            if not (build / 'deploy/html/workspace/index.html').is_file():
                self.die('Documentation is not built; run west docs build first')
            self.inf('Open http://127.0.0.1:8000/html/workspace/ (Ctrl+C to stop)')
            commands = [
                [
                    sys.executable,
                    '-m',
                    'http.server',
                    '8000',
                    '--bind',
                    '127.0.0.1',
                    '--directory',
                    str(build / 'deploy'),
                ]
            ]
        for command in commands:
            try:
                subprocess.run(command, cwd=workspace, env=env, check=True)
            except (OSError, subprocess.CalledProcessError) as error:
                self.die(str(error))
        if args.stage == 'build':
            try:
                publish_safety(workspace)
            except (OSError, ValueError) as error:
                self.die(str(error))
            self.inf('Documentation:', build / 'deploy/html/workspace/index.html')
