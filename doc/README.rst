Workspace documentation
=======================

One Sphinx project, configured by ``conf.py``, builds the workspace guides and a
single Sphinx-needs export. Requirements, C code and tests stay beside their
applications or samples; the pages here import and describe them.

For a new clone, start with the `repository README <../README.rst>`__. The built
site opens with an application overview and a getting-started guide. Install
CMake, Ninja, Doxygen and Graphviz (``dot``) on the host before building.

From the west workspace root (``<rootdir>``), build and view the application
guides without running the WarmGuard safety workflow:

.. code-block:: sh

   source .venv/bin/activate
   west update zdocs
   pip install -r ws-zephyr/doc/requirements.txt
   west docs build
   west docs show


Open http://127.0.0.1:8000/html/workspace/; stop the server with Ctrl+C.
This build includes WarmGuard's requirements and generated implementation/test
specification. It does not execute tests and does not require Affirmatrix or an
assurance case. After ``west safety build`` generates test and review pages, a
separate review snapshot is linked from WarmGuard in the same site. The `WarmGuard walkthrough
<../applications/warmguard/README.rst>`__
explains how to produce and review them.

The general site is under ``<rootdir>/build/docs-general/``; the WarmGuard safety
workflow generates pages under ``<rootdir>/build/docs/`` and copies them into the
general deploy tree. That deploy tree can be copied without the safety build
directory. Run ``west safety status`` to update the copied review snapshot. Test output is under
``twister-out/``, and temporary files and workflow caches under ``tmp/``. The single
general needs export is ``build/docs-general/deploy/html/workspace/needs.json``. Doxygen C
references are built from ``applications/warmguard/docs/api/Doxyfile.in`` and
``applications/warmguard/docs/tests/Doxyfile.in`` in the repository.

The persistent case and evidence bundles share their own Git repository at
``<rootdir>/assurance/warmguard/``. Commit and back it up separately from
the source repository, and keep it when cleaning builds.

Add future guides to ``index.rst`` and keep their configuration in the existing
``conf.py``. WarmGuard's pages share the same need vocabulary.

Run the workflow checks with:

.. code-block:: sh

   python -m unittest discover -s ws-zephyr/tests/workflow -v


Edit the guides
---------------

- ``index.rst`` introduces the applications; ``getting-started.rst`` covers setup.
- Application guides live in ``applications/<name>/docs/index.rst`` relative to
  the repository root, next to their source, configuration and tests.
- WarmGuard's requirements guide, generated references and walkthrough live in
  ``applications/warmguard/docs/``. Its Doxygen templates live there too.
- ``workspace_doc_pages`` in ``conf.py`` aggregates these sources into the site.
  Application entry pages are discovered automatically; additional sample and
  test guides can be registered in that mapping.
- Aggregation stages pages in the build tree only. Existing URLs
  (``applications/<name>.html`` and ``warmguard/<page>.html``) stay the same.
  Relative Sphinx links use these site paths; include/image paths are relative
  to the authored source file. Do not edit staged pages.
- WarmGuard requirements are authored in
  ``applications/warmguard/docs/requirements.sdoc`` relative to the repository root.
  The build imports their statements and relationships; do not copy them into RST.
- WarmGuard-specific Sphinx extensions live in
  ``applications/warmguard/docs/_extensions/``; only shared aggregation tooling
  stays in ``doc/_extensions/``.
- Code and test links come from source annotations. Rebuild after changing them.

``west docs build`` refreshes the general guides. ``west safety build`` refreshes the
selected test report and review snapshot as well. Neither command affirms links;
reviews are an explicit human step described in the walkthrough.

To aggregate a sample or test guide, keep its source beside that component and
add a site path to ``workspace_doc_pages``, for example:

.. code-block:: python

   workspace_doc_pages['samples/example'] = str(DOC.parent / 'samples/example/docs/index.rst')


Add ``samples/example`` to a toctree in the general index. The same convention
works for ``tests/<suite>/docs/index.rst``. No application, sample or test prose
needs to be stored under ``doc/``.
