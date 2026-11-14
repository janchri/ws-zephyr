# SPDX-License-Identifier: Apache-2.0
"""WarmGuard report rendering and links to the generated review snapshot."""

from pathlib import Path


def setup(app):
    app.setup_extension('test_module')

    from docutils import nodes
    from sphinx.util.docutils import SphinxDirective
    from test_module import TestReportDirective, TwisterInfoDirective

    class WarmGuardSafetyLinks(SphinxDirective):
        def run(self):
            if not self.config.warmguard_safety_available:
                return [
                    nodes.note(
                        '',
                        nodes.paragraph(
                            text='The test and review snapshot has not been built. '
                            'Run west safety prepare, then refresh this page.'
                        ),
                    )
                ]
            paragraph = nodes.paragraph(text='Open the generated test and review snapshot:')
            entries = nodes.bullet_list()
            for label, page in [
                ('Fictional medical-device workflow', 'mdr-workflow'),
                ('Requirements and review status', 'requirements'),
                ('Design and implementation', 'implementation'),
                ('Test specification', 'test-specification'),
                ('Test report', 'test-report'),
            ]:
                link = nodes.reference(
                    '', label, refuri=f'/safety/html/workspace/warmguard/{page}.html'
                )
                entries += nodes.list_item('', nodes.paragraph('', '', link))
            return [paragraph, entries]

    def input_exists(directive):
        env = directive.state.document.settings.env
        path = Path(directive.arguments[0])
        if not path.is_absolute():
            path = Path(env.app.config.twister_output_dir) / path
        env.note_dependency(str(path))
        return path.is_file()

    class LocalTestReport(TestReportDirective):
        def run(self):
            # A cold index build cannot consume its own not-yet-written export.
            # Report results are added during HTML rendering, after that export exists.
            if self.state.document.settings.env.app.builder.name == 'xref':
                return []
            if not input_exists(self):
                return [
                    nodes.paragraph(
                        text='No test report is selected for this build. Run west safety prepare, '
                        'then open the generated review snapshot from the WarmGuard page.'
                    )
                ]
            return super().run()

    class LocalTwisterInfo(TwisterInfoDirective):
        def run(self):
            if not input_exists(self):
                return []
            return super().run()

    app.add_directive('testreport', LocalTestReport, override=True)
    app.add_directive('twisterinfo', LocalTwisterInfo, override=True)
    app.add_config_value('warmguard_safety_available', False, 'env', types=[bool])
    app.add_directive('warmguard-safety-links', WarmGuardSafetyLinks)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': True}
