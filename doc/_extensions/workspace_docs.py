# SPDX-License-Identifier: Apache-2.0
"""Aggregate component-owned guides without storing content in doc/."""

from pathlib import Path
from tempfile import TemporaryDirectory

from sphinx.errors import ConfigError
from zephyr.external_content import adjust_includes


def keep_pages(app, config):
    # external_content runs before us and removes stale staged files. Keep only
    # the currently registered pages so incremental builds retain their mtimes.
    config.external_content_keep = [
        *config.external_content_keep,
        *(f'{name}.rst' for name in config.workspace_doc_pages),
    ]


def aggregate_pages(app):
    destination = Path(app.srcdir)
    if destination.resolve() == Path(__file__).resolve().parents[1]:
        raise ConfigError('Use west docs build to build documentation in a staged source tree')
    for name, source in app.config.workspace_doc_pages.items():
        source = Path(source).resolve()
        if not source.is_file():
            raise ConfigError(f'Missing documentation source for {name}: {source}')
        target = destination / f'{name}.rst'
        target.parent.mkdir(parents=True, exist_ok=True)
        # Apply the same include/image path handling as the shared staging
        # extension, retaining the original source directory for assets.
        with TemporaryDirectory() as temporary:
            staged = Path(temporary) / target.name
            staged.write_text(source.read_text(encoding='utf-8'), encoding='utf-8')
            adjust_includes(
                staged,
                source.parent,
                app.config.external_content_directives,
                app.config.source_encoding,
                dstpath=target.parent,
            )
            content = staged.read_text(encoding='utf-8')
        if not target.exists() or target.read_text(encoding='utf-8') != content:
            target.write_text(content, encoding='utf-8')


def setup(app):
    app.add_config_value('workspace_doc_pages', {}, 'env', types=[dict])
    app.connect('config-inited', keep_pages)
    app.connect('builder-inited', aggregate_pages, priority=600)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': True}
