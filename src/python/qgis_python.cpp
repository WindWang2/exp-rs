// src/python/qgis_python.cpp
#include "qgis_python.h"

#include <Python.h>
#include <pybind11/embed.h>
#include <QString>
#include <QDebug>

namespace py = pybind11;

QgisPython &QgisPython::instance()
{
    static QgisPython sInstance;
    return sInstance;
}

bool QgisPython::initialize()
{
    if (m_initialized)
        return true;

    if (!Py_IsInitialized())
    {
        py::initialize_interpreter();
        qDebug() << "Python interpreter initialized";
    }

    // Redirect stdout/stderr to capture output
    try
    {
        py::exec(R"(
import sys
from io import StringIO

class _ConsoleOutput:
    def __init__(self):
        self.buffer = StringIO()
    def write(self, text):
        self.buffer.write(text)
    def flush(self):
        pass
    def getvalue(self):
        val = self.buffer.getvalue()
        self.buffer = StringIO()
        return val

sys.stdout = _ConsoleOutput()
sys.stderr = _ConsoleOutput()
)");
        m_initialized = true;
        qDebug() << "Python stdout/stderr redirected";
    }
    catch (py::error_already_set &e)
    {
        qWarning() << "Failed to initialize Python console output:" << e.what();
        m_initialized = true; // Still mark as initialized, console output just won't capture
    }

    return true;
}

void QgisPython::finalize()
{
    if (!m_initialized)
        return;

    if (Py_IsInitialized())
    {
        py::finalize_interpreter();
        qDebug() << "Python interpreter finalized";
    }

    m_initialized = false;
}

bool QgisPython::runString(const QString &command, QString &output, QString &error)
{
    if (!m_initialized)
    {
        error = QStringLiteral("Python not initialized");
        return false;
    }

    try
    {
        py::object scope = py::globals();
        py::exec(command.toUtf8().constData(), scope);

        // Capture stdout
        py::object stdout_obj = py::module_::import("sys").attr("stdout");
        output = QString::fromStdString(stdout_obj.attr("getvalue")().cast<std::string>());

        // Capture stderr
        py::object stderr_obj = py::module_::import("sys").attr("stderr");
        error = QString::fromStdString(stderr_obj.attr("getvalue")().cast<std::string>());

        return true;
    }
    catch (py::error_already_set &e)
    {
        error = QString::fromUtf8(e.what());
        return false;
    }
}

void init_python_bindings()
{
    // Sub-modules are auto-registered via pybind11 PYBIND11_MODULE macros
    // This function triggers their initialization by importing them
    try
    {
        py::module_::import("qgis");
        qDebug() << "Python qgis module loaded";
    }
    catch (py::error_already_set &e)
    {
        qWarning() << "Failed to load Python qgis module:" << e.what();
    }
}
