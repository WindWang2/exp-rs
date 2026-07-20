"""SICNU GEO RS operator Python bindings.

This package re-exports the C++ pybind11 extension module `_sicnu_operators`
so users can simply write:

    import sicnu_operators as so
    so.list_operators()
"""
from _sicnu_operators import *  # noqa: F401,F403
from _sicnu_operators import __doc__  # noqa: F401
