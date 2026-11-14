# SPDX-License-Identifier: Apache-2.0
"""Import the authored .sdoc into Sphinx-needs without duplicating requirements."""

from pathlib import Path

from sphinx.util.docutils import SphinxDirective
from sphinx_needs.api import add_need
from strictdoc.backend.sdoc.reader import SDReader


class StrictDocRequirements(SphinxDirective):
    has_content = False

    def run(self):
        source = Path(self.config.demo_sdoc)
        self.env.note_dependency(str(source))
        document = SDReader.read(source.read_text(encoding='utf-8'))
        result = []
        for requirement in document.iterate_nodes():
            parents = [ref.ref_uid for ref in requirement.relations if ref.ref_type == 'Parent']
            result.extend(
                add_need(
                    self.env.app,
                    self.state,
                    self.env.docname,
                    self.lineno,
                    need_type='req',
                    layout='zdocs_requirement',
                    id=requirement.reserved_uid,
                    title=requirement.reserved_title,
                    content=requirement.reserved_statement,
                    refines=parents,
                )
            )
        return result


def setup(app):
    app.add_config_value('demo_sdoc', '', 'env')
    app.add_directive('strictdoc-requirements', StrictDocRequirements)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': True}
