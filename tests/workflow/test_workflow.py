# SPDX-License-Identifier: Apache-2.0
"""Behavioral checks for the workspace documentation and safety commands."""

import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

SCRIPTS = Path(__file__).resolve().parents[2] / 'scripts'
spec = importlib.util.spec_from_file_location('workspace_safety', SCRIPTS / 'safety.py')
safety_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(safety_module)
from docs_artifacts import publish_safety  # noqa: E402


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        temporary = SCRIPTS.parent.parent / 'tmp'
        temporary.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=temporary)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.safety = safety_module.Safety()
        self.safety.workspace = self.root
        self.safety.case = self.root / 'assurance/warmguard'
        self.safety.repo = self.root / 'ws-zephyr'
        self.safety.demo = self.safety.repo / 'applications/warmguard'
        self.safety.temporary = self.root / 'tmp'
        self.safety.env = os.environ.copy()
        self.safety.inf = Mock()
        self.safety.wrn = Mock()
        self.safety.die = Mock(side_effect=SystemExit(1))

    def test_prepare_does_not_refresh_a_second_time(self):
        self.safety.stage_verify = Mock()
        self.safety.stage_build = Mock()
        self.safety.stage_status = Mock()
        args = SimpleNamespace(run='baseline')
        self.safety.stage_prepare(args)
        self.safety.stage_verify.assert_called_once_with(args)
        self.safety.stage_build.assert_called_once_with(args)
        self.safety.stage_status.assert_called_once_with(args, refresh=False)

    def test_failed_verification_preserves_named_evidence(self):
        self.safety.git = Mock(
            side_effect=lambda repo, *args: 'a' * 40 if args[0] == 'rev-parse' else ''
        )

        def twister(command, **kwargs):
            output = self.root / 'twister-out/warmguard-failed'
            output.mkdir(parents=True)
            (output / 'twister.json').write_text('{"testsuites": []}')
            return SimpleNamespace(returncode=1)

        self.safety._exec = twister
        with self.assertRaises(SystemExit):
            self.safety.stage_verify(SimpleNamespace(run='failed'))
        bundle = self.safety.case / 'runs/failed'
        self.assertTrue((bundle / 'twister.json').is_file())
        self.assertEqual(json.loads((bundle / 'context.json').read_text())['twister_exit_code'], 1)

    def test_missing_case_blocks_review(self):
        with self.assertRaisesRegex(ValueError, 'No initialized assurance case'):
            self.safety.refresh_for_review()

    def test_affirm_refreshes_the_website_after_writing_draft(self):
        order = []
        self.safety.refresh_for_review = lambda: order.append('before')
        self.safety.aff = lambda *args: order.append('affirm')
        self.safety.current_report = lambda: self.root / 'report'
        self.safety.docs = lambda report: order.append(('after', report))
        args = SimpleNamespace(
            kind='Implements',
            from_id='IMPL-x',
            to_id='REQ-x',
            below=None,
            role='Reviewer',
            reason='Reviewed the link',
            revision=None,
        )
        self.safety.stage_affirm(args)
        self.assertEqual(order, ['before', 'affirm', ('after', self.root / 'report')])

    def test_preview_does_not_initialize_or_stage_a_repository(self):
        (self.safety.case / 'nodes').mkdir(parents=True)
        (self.safety.case / 'nodes/example.jsonld').write_text('{}')
        self.safety.aff = Mock()
        self.safety.stage_record(SimpleNamespace(case=True, run=[], message=None, preview=True))
        self.assertFalse((self.safety.case / '.git').exists())
        self.assertEqual(self.safety.aff.call_count, 2)

    def test_preview_preserves_staged_and_untracked_review_records(self):
        events = self.safety.case / 'events'
        events.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(self.safety.case)], check=True)
        (events / 'staged.jsonld').write_text('{}')
        subprocess.run(
            ['git', '-C', str(self.safety.case), 'add', 'events/staged.jsonld'], check=True
        )
        (events / 'draft.jsonld').write_text('{}')
        self.safety.aff = Mock()
        self.safety._exec = Mock(return_value=SimpleNamespace(returncode=0))
        self.safety.stage_record(SimpleNamespace(case=True, run=[], message=None, preview=True))
        status = subprocess.run(
            ['git', '-C', str(self.safety.case), 'status', '--porcelain'],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn('A  events/staged.jsonld', status)
        self.assertIn('?? events/draft.jsonld', status)
        commands = [call.args[0] for call in self.safety._exec.call_args_list]
        self.assertTrue(any('--cached' in command for command in commands))
        self.assertFalse(any('add' in command or 'commit' in command for command in commands))

    def test_published_safety_site_is_a_copy_and_is_refreshed(self):
        source = self.root / 'build/docs/deploy/html/workspace/applications'
        source.mkdir(parents=True)
        (source / 'warmguard.html').write_text('first')
        deploy = self.root / 'build/docs-general/deploy'
        deploy.mkdir(parents=True)
        target = deploy / 'safety/html/workspace/applications/warmguard.html'
        self.assertTrue(publish_safety(self.root))
        self.assertFalse((deploy / 'safety').is_symlink())
        self.assertEqual(target.read_text(), 'first')
        (source / 'warmguard.html').write_text('second')
        self.assertEqual(target.read_text(), 'first')
        publish_safety(self.root)
        self.assertEqual(target.read_text(), 'second')

    def test_missing_safety_site_does_not_create_copy(self):
        deploy = self.root / 'build/docs-general/deploy'
        deploy.mkdir(parents=True)
        self.assertFalse(publish_safety(self.root))
        self.assertFalse((deploy / 'safety').exists())

    def test_publisher_does_not_replace_unknown_directory(self):
        source = self.root / 'build/docs/deploy/html/workspace/applications'
        source.mkdir(parents=True)
        (source / 'warmguard.html').touch()
        target = self.root / 'build/docs-general/deploy/safety'
        target.mkdir(parents=True)
        (target / 'keep').write_text('user data')
        with self.assertRaisesRegex(ValueError, 'not a generated snapshot'):
            publish_safety(self.root)
        self.assertEqual((target / 'keep').read_text(), 'user data')


if __name__ == '__main__':
    unittest.main()
