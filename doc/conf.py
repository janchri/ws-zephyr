# SPDX-License-Identifier: Apache-2.0
"""One Sphinx project for ws-zephyr, with a shared traceability vocabulary."""

import os
import sys
from pathlib import Path

DOC = Path(__file__).resolve().parent
sys.path.insert(0, os.environ['ZDOCS_CONF_DIR'])
sys.path.insert(0, str(DOC / '_extensions'))
sys.path.insert(0, str(DOC.parent / 'applications/warmguard/docs/_extensions'))
from zdocs_conf import configure, need_layout  # noqa: E402

configure(
    globals(),
    doc_dir=DOC,
    project='ws-zephyr',
    extensions=['workspace_docs', 'strictdoc_needs', 'affirmatrix_status', 'warmguard_docs'],
    static_path=[DOC / '_static'],
    css_files=['workspace.css'],
)
globals().update(
    needs_types=[
        dict(directive=name, title=title, prefix=prefix, color=color, style='node')
        for name, title, prefix, color in [
            ('req', 'Requirement', 'REQ-', '#FDEBD0'),
            ('impl', 'Implementation', 'IMPL-', '#E2EFDA'),
            ('test_case', 'Test case', 'TC-', '#D6E4F7'),
            ('test_procedure', 'Test procedure', 'TP-', '#D6E4F7'),
            ('test_result', 'Test result', 'TR-', '#E2EFDA'),
        ]
    ],
    needs_id_regex=r'^[A-Za-z][A-Za-z0-9_-]+$',
    needs_links={
        name: dict(description=name, outgoing=name, incoming=incoming)
        for name, incoming in [
            ('refines', 'refined by'),
            ('satisfies', 'satisfied by'),
            ('verifies', 'verified by'),
            ('result_of', 'results'),
            ('covers', 'covered by'),
        ]
    },
    needs_fields={
        name: {'schema': {'type': 'string'}, 'nullable': True}
        for name in [
            'test_function',
            'test_module',
            'suite',
            'suite_title',
            'platform',
            'scenario',
            'twister_id',
            'execution_time',
            'reason',
        ]
    },
    needs_layouts={'zdocs_requirement': need_layout(fields=[])},
    needs_reproducible_json=True,
    needs_json_remove_defaults=False,
    # An export is one build, not a changing Git-derived version key.
    version='demo',
    release='demo',
)

# Map application-owned sources to stable URLs in the aggregated site.
# Samples and tests can add their own sources to this mapping as guides appear.
workspace_doc_pages = {
    f'applications/{app.name}': str(app / 'docs/index.rst')
    for app in sorted((DOC.parent / 'applications').iterdir())
    if (app / 'docs/index.rst').is_file()
}
workspace_doc_pages.update(
    {
        f'warmguard/{page.stem}': str(page)
        for page in sorted((DOC.parent / 'applications/warmguard/docs').glob('*.rst'))
        if page.stem != 'index'
    }
)

# The requirements are authored with their application, not copied into doc/.
demo_sdoc = str(DOC.parent / 'applications/warmguard/docs/requirements.sdoc')
affirmatrix_status_config = str(DOC.parent / 'applications/warmguard/affirmatrix.yaml')
# Stage 1 exports this site's specification; HTML uses it for report correlation.
testspec_needs_json = str(Path(os.environ['ZDOCS_DOC_DEPLOY_DIR']) / 'html/workspace/needs.json')
exclude_patterns = [
    '_extensions',
    'CMakeLists.txt',
    'documents.yaml',
    'requirements.txt',
    'README.rst',
]
