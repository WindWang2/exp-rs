"""
Root conftest: preloads system Qt 6.11.1 before pytest-qt can load PySide6's
bundled Qt 6.11.1 (different GCC ABI). Also intercepts session finish to call
_Exit() and bypass the QGIS/PROJ teardown crash in libqgis_core.so DSO dtors.
"""
import ctypes
import os


def pytest_load_initial_conftests(early_config, parser, args):
    """Fires before plugins configure — preload system Qt first."""
    for lib in [
        "/usr/lib/libQt6Core.so.6",
        "/usr/lib/libQt6DBus.so.6",
        "/usr/lib/libQt6Gui.so.6",
        "/usr/lib/libQt6Widgets.so.6",
        "/usr/lib/libQt6Network.so.6",
        "/usr/lib/libQt6Xml.so.6",
        "/usr/lib/libQt6Svg.so.6",
        "/usr/lib/libQt6Sql.so.6",
        "/usr/lib/libQt6Concurrent.so.6",
        "/usr/lib/libQt6Multimedia.so.6",
        "/usr/lib/libQt6Core5Compat.so.6",
        "/usr/lib/libQt6OpenGL.so.6",
    ]:
        try:
            ctypes.CDLL(lib, ctypes.RTLD_GLOBAL)
        except OSError:
            pass


def pytest_sessionfinish(session, exitstatus):
    """Bypass QGIS DSO teardown crash by skipping Python cleanup.
    The crash in QgsCoordinateTransformPrivate::freeProj() happens after
    all tests complete, during Python's own exit processing of deferred
    Qt object deletions.  _exit() skips that entirely.
    """
    os._exit(int(exitstatus))
