# SPDX-License-Identifier: Apache-2.0
"""Render a read-only, build-time view of Affirmatrix relationship states."""

import json
import subprocess
import sys
from collections import Counter
from datetime import UTC, datetime
from pathlib import Path

from docutils import nodes
from sphinx.util.docutils import SphinxDirective


class AffirmatrixStatus(SphinxDirective):
    def run(self):
        # The native readers need the site's complete stage-1 needs export.
        if self.env.app.builder.name == 'xref':
            return []
        if not self.config.affirmatrix_status_enabled:
            return [
                nodes.note(
                    '',
                    nodes.paragraph(
                        text='Review status is not included in the general documentation build. '
                        'Use west safety build to refresh the WarmGuard review status.'
                    ),
                )
            ]
        try:
            from affirmatrix.config import load
        except ModuleNotFoundError:
            return [
                nodes.note(
                    '',
                    nodes.paragraph(
                        text='Review status unavailable. Install the WarmGuard optional tools.'
                    ),
                )
            ]

        path = Path(self.config.affirmatrix_status_config)
        self.env.note_dependency(str(path))
        config = load(path)
        self.env.note_dependency(str(config.case))
        if not config.case.is_dir():
            return [
                nodes.note(
                    '',
                    nodes.paragraph(
                        text='No assurance case initialized. Run west safety build first.'
                    ),
                )
            ]
        for record in config.case.rglob('*.jsonld'):
            self.env.note_dependency(str(record))
        # Affirmatrix derives working-tree status, including uncommitted drafts.
        # Its case history is authoritative only when this directory is the repo root.
        git = subprocess.run(
            ['git', '-C', str(config.case), 'rev-parse', '--show-toplevel'],
            capture_output=True,
            text=True,
            check=False,
        )
        case_is_repo = (
            git.returncode == 0 and Path(git.stdout.strip()).resolve() == config.case.resolve()
        )
        draft = True
        if case_is_repo:
            head = subprocess.run(
                ['git', '-C', str(config.case), 'rev-parse', '--verify', 'HEAD'],
                capture_output=True,
                text=True,
                check=False,
            )
            check = subprocess.run(
                [
                    'git',
                    '-C',
                    str(config.case),
                    'status',
                    '--porcelain',
                    '--',
                    'context.jsonld',
                    'schema',
                    'nodes',
                    'edges',
                    'events',
                    'proofs',
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            draft = head.returncode != 0 or check.returncode != 0 or bool(check.stdout.strip())
        if config.producer is not None:
            for reader in [
                config.producer.requirements,
                config.producer.implementations,
                config.producer.specifications,
            ]:
                if reader is not None:
                    self.env.note_dependency(str(reader.export))

        result = subprocess.run(
            [
                sys.executable,
                '-m',
                'affirmatrix.cli',
                '--config',
                str(path),
                'graph',
                'status',
                '--json',
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        # Exit 1 is a valid report containing stale/broken edges, not a build error.
        if result.returncode not in (0, 1):
            raise self.error(f'Affirmatrix status failed: {result.stderr or result.stdout}')
        try:
            edges = json.loads(result.stdout)['edges']
        except (ValueError, KeyError) as error:
            raise self.error(
                f'Affirmatrix returned no valid status report: {result.stdout}'
            ) from error

        summary = nodes.paragraph()
        counts = Counter(edge.get('state', 'vanished') for edge in edges)
        for index, (state, count) in enumerate(sorted(counts.items())):
            if index:
                summary += nodes.Text(' · ')
            summary += nodes.Text(f'{count} ')
            summary += _badge(state)

        table = nodes.table(classes=['affirmatrix-status-table'])
        group = nodes.tgroup(cols=4)
        table += group
        for width in [30, 15, 30, 25]:
            group += nodes.colspec(colwidth=width)
        head = nodes.thead()
        row = nodes.row()
        for title in ['From', 'Relationship', 'To', 'Review state']:
            row += nodes.entry('', nodes.paragraph(text=title))
        head += row
        group += head
        body = nodes.tbody()
        group += body
        for edge in edges:
            row = nodes.row()
            for key in ['from', 'kind', 'to']:
                row += nodes.entry('', nodes.paragraph('', '', nodes.literal(text=edge[key])))
            row += nodes.entry('', nodes.paragraph('', '', _badge(edge.get('state', 'vanished'))))
            body += row
        rendered = datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S UTC')
        report_dir = (
            Path(self.config.twister_output_dir) if self.config.twister_output_dir else None
        )
        if report_dir and (report_dir / 'run.name').is_file():
            report = f"named run {(report_dir / 'run.name').read_text(encoding='utf-8').strip()}"
        elif report_dir and (report_dir / 'twister.json').is_file():
            report = 'working-tree test report'
        else:
            report = 'no test report selected'
        output = [
            nodes.note(
                '',
                nodes.paragraph(
                    text=f'Review snapshot rendered {rendered}; selected report: {report}. '
                    'Results and reviews can change after this snapshot; rebuild or run '
                    'west safety status.'
                ),
            )
        ]
        if draft:
            output.append(
                nodes.note(
                    '',
                    nodes.paragraph(
                        text='Case records include uncommitted drafts or have no case repository. '
                        'Commit reviewed records to preserve the case history.'
                    ),
                )
            )
        output.extend([summary, table])
        return output


def _badge(state):
    return nodes.inline(text=state, classes=['affirmatrix-state', f'affirmatrix-{state.lower()}'])


def setup(app):
    app.add_config_value('affirmatrix_status_config', '', 'env')
    app.add_config_value('affirmatrix_status_enabled', False, 'env', types=[bool])
    app.add_directive('affirmatrix-status', AffirmatrixStatus)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': True}
