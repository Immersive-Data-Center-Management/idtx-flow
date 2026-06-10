"""Marker for the ``scons`` Python package.

The legacy SCons tool files (godotcpp.py, mdlsdk.py, ixwebsocket.py, ...)
sit directly in this directory and continue to work because SCons resolves
them via its ``toolpath`` lookup, which doesn't require the directory to
be a Python package.

This ``__init__.py`` is needed only so that ``scons.core``,
``scons.platforms``, and ``scons.tools.*`` can be imported by the
*refactored* tools (under ``scons/tools/``) using normal Python imports.
"""