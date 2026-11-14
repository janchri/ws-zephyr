# SPDX-License-Identifier: Apache-2.0
"""User-facing safety workflow for the WarmGuard demonstration."""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from west.commands import WestCommand

sys.path.insert(0, str(Path(__file__).resolve().parent))
from docs_artifacts import publish_safety  # noqa: E402


class Safety(WestCommand):
    def __init__(self):
        super().__init__(
            'safety',
            'build, verify and review the requirements example',
            'Build documentation, capture verification evidence, and review traceability.',
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, help=self.help, description=self.description)
        commands = parser.add_subparsers(dest='stage', required=True)
        prepare = commands.add_parser('prepare', help='run tests, build the site and check status')
        prepare.add_argument('--run', help='save evidence under this unique run name')
        build = commands.add_parser('build', help='build documentation and synchronize the graph')
        build.add_argument('--run', help='render the report for a captured run')
        verify = commands.add_parser('verify', help='run verification; optionally capture evidence')
        verify.add_argument('--run', help='unique run name; requires clean source checkouts')
        status = commands.add_parser('status', help='show relationship and evidence status')
        status.add_argument('--run', help='include a captured run in the assessment')
        review = commands.add_parser('review', help='inspect relationships before affirming')
        self.selectors(review)
        review.add_argument('--node', help='show current content and provenance for one node')
        review.add_argument('-v', '--verbose', action='store_true', help='show full review details')
        affirm = commands.add_parser('affirm', help='affirm explicitly selected relationships')
        self.selectors(affirm)
        affirm.add_argument('--role', required=True, help='your review role')
        affirm.add_argument('--reason', required=True, help='what you reviewed and why it holds')
        affirm.add_argument('--revision', help='explicit, unverified source revision override')
        record = commands.add_parser('record', help='commit selected assurance records and runs')
        record.add_argument('--case', action='store_true', help='include case and review records')
        record.add_argument('--run', action='append', default=[], help='include this named run')
        record.add_argument('--message', '-m', help='your commit message')
        record.add_argument('--preview', action='store_true', help='show selection without staging')
        commands.add_parser('assess', help='check completeness of the fictional exercise fixture')
        return parser

    @staticmethod
    def selectors(parser):
        parser.add_argument('--kind', help='relationship kind: Refines, Implements or Verifies')
        parser.add_argument('--from', dest='from_id', help='source node')
        parser.add_argument('--to', dest='to_id', help='target node')
        parser.add_argument('--below', help='select the subtree below a requirement')

    def do_run(self, args, unknown_args):
        if unknown_args:
            self.die('Unrecognized arguments:', ' '.join(unknown_args))
        self.workspace = Path(self.topdir).resolve()
        self.repo = Path(__file__).resolve().parent.parent
        self.demo = self.repo / 'applications/warmguard'
        self.case = self.workspace / 'assurance/warmguard'
        self.doc_build = self.workspace / 'build/docs'
        self.default_test = self.workspace / 'twister-out/warmguard'
        self.temporary = self.workspace / 'tmp'
        self.temporary.mkdir(parents=True, exist_ok=True)
        (self.temporary / 'cache').mkdir(exist_ok=True)
        self.env = dict(
            os.environ,
            TMPDIR=str(self.temporary),
            PYTHONPYCACHEPREFIX=str(self.temporary / 'pycache'),
            XDG_CACHE_HOME=str(self.temporary / 'cache'),
        )
        self.affirmatrix_config = self.demo / 'affirmatrix.yaml'
        try:
            getattr(self, f'stage_{args.stage}')(args)
        except subprocess.CalledProcessError as error:
            self.die(str(error), exit_code=error.returncode)
        except (OSError, ValueError) as error:
            self.die(str(error))

    def _exec(self, command, *, check=True, capture=False):
        if not capture:
            self.inf('+', shlex.join(str(x) for x in command))
        return subprocess.run(
            [str(x) for x in command],
            cwd=self.workspace,
            env=self.env,
            check=check,
            text=True,
            capture_output=capture,
        )

    def git(self, repo, *args):
        return self._exec(['git', '-C', repo, *args], capture=True).stdout.strip()

    def aff(self, *args, check=True, capture=False):
        return self._exec(
            [sys.executable, '-m', 'affirmatrix.cli', '--config', self.affirmatrix_config, *args],
            check=check,
            capture=capture,
        )

    def run_paths(self, name):
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', name):
            raise ValueError('Run names may contain letters, digits, hyphens and underscores only')
        return (
            self.case / 'runs' / name,
            self.workspace / 'twister-out' / f'warmguard-{name}',
        )

    def report_dir(self, name):
        if name is None:
            return self.default_test
        bundle, _ = self.run_paths(name)
        if not (bundle / 'twister.json').is_file():
            raise ValueError(f'Run {name!r} has no Twister evidence bundle')
        return bundle

    def docs(self, report):
        self.inf('Refreshing documentation and traceability inputs...')
        commands = [
            [
                'cmake',
                '-S',
                self.repo / 'doc',
                '-B',
                self.doc_build,
                f'-DZephyr_DIR={self.workspace}/zephyr/share/zephyr-package/cmake',
                f'-DZEPHYR_BASE={self.workspace}/zephyr',
                f'-DUSER_CACHE_DIR={self.temporary}/zephyr-cache',
                f'-DZDOCS_TWISTER_OUT={report}',
                '-DZDOCS_SPHINXOPTS=-q -j 1 -W --keep-going '
                '-D affirmatrix_status_enabled=1 -t warmguard_assurance',
            ],
            ['cmake', '--build', self.doc_build, '--target', 'all-docs', '--parallel', '2'],
        ]
        for command in commands:
            result = self._exec(command, check=False, capture=True)
            if result.returncode:
                sys.stderr.write(result.stdout + result.stderr)
                raise subprocess.CalledProcessError(result.returncode, command)
            for line in result.stderr.splitlines():
                if 'warning' in line.lower():
                    self.wrn(line)
        self.inf('Documentation and cross-reference checks passed.')
        publish_safety(self.workspace)

    def current_report(self):
        """Keep the previously selected report when refreshing for a review."""
        cache = self.doc_build / 'CMakeCache.txt'
        if cache.is_file():
            for line in cache.read_text().splitlines():
                if line.startswith('ZDOCS_TWISTER_OUT:') and '=' in line:
                    return Path(line.split('=', 1)[1])
        return self.default_test

    def refresh_for_review(self, run=None):
        if not (self.case / 'schema').is_dir():
            raise ValueError('No initialized assurance case; run west safety build first')
        self.docs(self.report_dir(run) if run else self.current_report())

    def stage_build(self, args):
        report = self.report_dir(args.run)
        self.docs(report)
        self.aff('case', 'init')
        self.aff('case', 'check')
        self.aff('case', 'sync')
        self.aff('graph', 'check')
        # Render again after initialization/sync so the website has the current case.
        self.docs(report)
        self.inf('Updating the shared application site...')
        self._exec([sys.executable, '-m', 'west', 'docs', 'build'])
        self.inf('Documentation:', self.doc_build / 'deploy/html/workspace/index.html')
        self.inf(
            'The synchronized records are drafts until committed with west safety record --case.'
        )
        self.inf(
            'A valid graph can still contain pending reviews and does not establish readiness.'
        )

    def stage_prepare(self, args):
        # Reuse the same paths and provenance checks as the individual stages.
        self.stage_verify(args)
        self.stage_build(args)
        self.stage_status(args, refresh=False)

    def stage_assess(self, args):
        fixture = json.loads((self.demo / 'mdr-fixture.json').read_text(encoding='utf-8'))
        required = {
            'intended_use',
            'quality_system',
            'risk',
            'verification_validation',
            'clinical_usability_security',
            'tools_dependencies',
            'technical_documentation',
            'release_postmarket',
        }
        records = fixture.get('records', {})
        self.inf('Fictional exercise completeness check; this is not an MDR assessment.')
        errors = []
        if fixture.get('simulation') is not True or not isinstance(records, dict):
            raise ValueError('Invalid fictional fixture: simulation and records are required')
        for key in sorted(required):
            record = records.get(key)
            if not isinstance(record, dict):
                errors.append(f'{key}: missing record')
                continue
            evidence = record.get('evidence')
            if (
                record.get('status') != 'simulated'
                or not isinstance(record.get('summary'), str)
                or not record['summary'].strip()
                or not isinstance(evidence, list)
                or not evidence
            ):
                errors.append(f'{key}: missing simulated status, summary or evidence')
                continue
            for path in evidence:
                if not isinstance(path, str) or not path.strip():
                    errors.append(f'{key}: invalid evidence path')
                    continue
                target = (self.repo / path).resolve()
                if not target.is_relative_to(self.repo.resolve()) or not target.is_file():
                    errors.append(f'{key}: missing or external evidence: {path}')
            self.inf('  SIMULATED:', key.replace('_', ' '), '-', record['summary'])
        if errors:
            for error in errors:
                self.wrn('OPEN:', error)
            self.die('Fictional fixture is incomplete.')
        self.inf('Fixture fields and referenced files are present.')
        self.inf('No test outcome, approval, clinical evidence, or legal conformity was verified.')

    def stage_verify(self, args):
        repositories = {'firmware': self.repo, 'zephyr': self.workspace / 'zephyr'}
        if args.run:
            bundle, output = self.run_paths(args.run)
            if bundle.exists() or output.exists():
                raise ValueError('Run name already exists; choose a new name')
            before = {
                name: (self.git(repo, 'rev-parse', 'HEAD'), self.git(repo, 'status', '--porcelain'))
                for name, repo in repositories.items()
            }
            dirty = [name for name, (_, status) in before.items() if status]
            if dirty:
                raise ValueError(
                    'Commit changes in these checkouts before capturing evidence: '
                    + ', '.join(dirty)
                )
        else:
            output = self.default_test
        command = [
            sys.executable,
            '-m',
            'west',
            'twister',
            '-p',
            'native_sim/native/64',
            '-T',
            self.demo,
            '--outdir',
            output,
            '--inline-logs',
            f'-x=USER_CACHE_DIR={self.temporary}/zephyr-cache',
        ]
        result = self._exec(command, check=False)
        if args.run:
            after = {
                name: (self.git(repo, 'rev-parse', 'HEAD'), self.git(repo, 'status', '--porcelain'))
                for name, repo in repositories.items()
            }
            if after != before:
                raise ValueError('A checkout changed during execution; no bundle was recorded')
            if not (output / 'twister.json').is_file():
                raise ValueError('Twister produced no report; no bundle was recorded')
            bundle.mkdir(parents=True)
            for name in ['twister.json', 'twister_report.xml', 'twister.xml']:
                if (output / name).is_file():
                    shutil.copy2(output / name, bundle / name)
            (bundle / 'run.name').write_text(args.run + '\n')
            (bundle / 'command.txt').write_text(shlex.join(str(x) for x in command) + '\n')
            for name, (revision, changes) in before.items():
                (bundle / f'{name}.sha').write_text(revision + '\n')
                (bundle / f'{name}.dirty').write_text(changes)
            (bundle / 'context.json').write_text(
                json.dumps(
                    {
                        'python': sys.version,
                        'repositories': {k: str(v) for k, v in repositories.items()},
                        'twister_exit_code': result.returncode,
                    },
                    indent=2,
                )
                + '\n'
            )
            self.inf('Evidence captured:', bundle)
        else:
            self.inf(
                'Working-tree test only; use --run NAME on clean checkouts to capture evidence.'
            )
        if result.returncode:
            self.die(
                'Verification failed; inspect the Twister report.', exit_code=result.returncode
            )

    def stage_status(self, args, *, refresh=True):
        if refresh:
            self.refresh_for_review(args.run)
        if not (self.case / '.git').exists():
            self.inf('Case records are drafts: no assurance Git repository has been initialized.')
        else:
            head = self._exec(
                ['git', '-C', self.case, 'rev-parse', '--verify', 'HEAD'],
                check=False,
                capture=True,
            )
            changed = self.git(
                self.case,
                'status',
                '--porcelain',
                '--',
                'context.jsonld',
                'schema',
                'nodes',
                'edges',
                'events',
                'proofs',
            )
            if head.returncode or changed:
                self.inf('Case records contain uncommitted drafts; record them after review.')
        command = ['graph', 'status']
        if args.run:
            bundle, _ = self.run_paths(args.run)
            if not bundle.is_dir():
                raise ValueError(f'No evidence bundle for run {args.run!r}')
            command += ['--bundle', str(bundle)]
        result = self.aff(*command, check=False)
        if result.returncode:
            self.die(
                'Status reports changed, broken or unavailable relationships.',
                exit_code=result.returncode,
            )
        if args.run:
            result = self.aff(
                'proof', 'check', '--scope', 'DEMO-SYS-001', '--bundle', str(bundle), check=False
            )
            if result.returncode:
                self.die(
                    'Evidence gate is blocked or could not be evaluated.',
                    exit_code=result.returncode,
                )

    @staticmethod
    def selection(args):
        result = []
        for name, value in [
            ('kind', args.kind),
            ('from', args.from_id),
            ('to', args.to_id),
            ('below', args.below),
        ]:
            if value:
                result += [f'--{name}', value]
        return result

    def stage_review(self, args):
        if args.node and (args.kind or args.from_id or args.to_id or args.below):
            raise ValueError('--node cannot be combined with relationship selectors')
        self.refresh_for_review()
        if args.node:
            command = ['node', 'show', args.node]
            if args.verbose:
                command.append('-v')
            result = self.aff(*command, check=False)
            if result.returncode not in (0, 1):
                self.die('The node could not be inspected.', exit_code=result.returncode)
        else:
            selection = self.selection(args) or ['--below', 'DEMO-SYS-001']
            command = ['edge', 'show', *selection]
            if args.verbose:
                command.append('-v')
            self.aff(*command)
            self.inf('Inspect endpoints with west safety review --node ID before affirming.')

    def stage_affirm(self, args):
        selection = self.selection(args)
        if not selection:
            raise ValueError('Select relationships using --below or --kind/--from/--to')
        if not args.role.strip() or not args.reason.strip():
            raise ValueError('A review role and a meaningful reason are required')
        self.refresh_for_review()
        command = ['edge', 'affirm', *selection, '--role', args.role, '--reason', args.reason]
        if args.revision:
            self.wrn(
                'Explicit revisions bypass Affirmatrix source revision and cleanliness checks.'
            )
            command += ['--revision', args.revision]
        self.aff(*command)
        self.docs(self.current_report())
        self.inf(
            'Draft review records written; use west safety record --case -m MESSAGE to commit.'
        )

    def stage_record(self, args):
        if not args.case and not args.run:
            raise ValueError('Select --case and/or --run NAME; nothing was committed')
        if not args.preview and (not args.message or not args.message.strip()):
            raise ValueError('A commit message is required')
        paths = []
        if args.case:
            paths += [
                name
                for name in [
                    'README.md',
                    'context.jsonld',
                    'schema',
                    'nodes',
                    'edges',
                    'events',
                    'proofs',
                ]
                if (self.case / name).exists()
            ]
        for name in args.run:
            bundle, _ = self.run_paths(name)
            if not bundle.is_dir():
                raise ValueError(f'No evidence bundle for run {name!r}')
            paths.append(f'runs/{name}')
        if not paths:
            raise ValueError('No selected case records exist; run west safety build first')
        if args.case:
            # Commit only a case that passes Affirmatrix's structural checks.
            self.aff('case', 'check')
            self.aff('graph', 'check')
        if args.preview:
            self.inf('Selected paths:', ', '.join(paths))
            if (self.case / '.git').exists():
                self._exec(
                    [
                        'git',
                        '-C',
                        self.case,
                        'status',
                        '--short',
                        '--untracked-files=all',
                        '--',
                        *paths,
                    ]
                )
                self._exec(['git', '-C', self.case, 'diff', '--', *paths])
                self._exec(['git', '-C', self.case, 'diff', '--cached', '--', *paths])
            else:
                self.inf('No assurance Git repository exists; selected files are new:')
                for name in paths:
                    item = self.case / name
                    for path in sorted(item.rglob('*')) if item.is_dir() else [item]:
                        if path.is_file():
                            self.inf('  ', path.relative_to(self.case))
            self.inf('Preview only: nothing staged or committed.')
            self.inf('Inspect untracked files separately.')
            return
        self.case.mkdir(parents=True, exist_ok=True)
        if not (self.case / '.git').exists():
            self._exec(['git', 'init', self.case])
        if (
            Path(self.git(self.case, 'rev-parse', '--show-toplevel')).resolve()
            != self.case.resolve()
        ):
            raise ValueError('The case must be the root of its own Git repository')
        if self.git(self.case, 'diff', '--cached', '--name-only'):
            raise ValueError(
                'Assurance repository has staged changes; review them before recording'
            )
        try:
            self.git(self.case, 'var', 'GIT_AUTHOR_IDENT')
            self.git(self.case, 'var', 'GIT_COMMITTER_IDENT')
        except subprocess.CalledProcessError as error:
            raise ValueError('Configure your Git author and committer identity first') from error
        self.inf('Recording selected paths:', ', '.join(paths))
        self._exec(['git', '-C', self.case, 'add', '--', *paths])
        self._exec(['git', '-C', self.case, 'diff', '--cached', '--stat'])
        self._exec(['git', '-C', self.case, 'commit', '-m', args.message])
