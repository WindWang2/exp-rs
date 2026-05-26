#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>

namespace pybind11::detail {

template <> struct type_caster<QString> {
public:
    PYBIND11_TYPE_CASTER(QString, const_name("str"));
    bool load(handle src, bool) {
        if (!PyUnicode_Check(src.ptr())) return false;
        value = QString::fromUtf8(PyUnicode_AsUTF8(src.ptr()));
        return true;
    }
    static handle cast(const QString &s, return_value_policy, handle) {
        return PyUnicode_FromString(s.toUtf8().constData());
    }
};

template <> struct type_caster<QStringList> {
public:
    PYBIND11_TYPE_CASTER(QStringList, const_name("list[str]"));
    bool load(handle src, bool) {
        if (!PyList_Check(src.ptr())) return false;
        for (Py_ssize_t i = 0; i < PyList_Size(src.ptr()); i++) {
            PyObject *item = PyList_GetItem(src.ptr(), i);
            if (!PyUnicode_Check(item)) return false;
            value << QString::fromUtf8(PyUnicode_AsUTF8(item));
        }
        return true;
    }
    static handle cast(const QStringList &sl, return_value_policy, handle) {
        list out;
        for (const QString &s : sl) out.append(s.toUtf8().constData());
        return out.release();
    }
};

template <> struct type_caster<QVariant> {
public:
    PYBIND11_TYPE_CASTER(QVariant, const_name("object"));
    bool load(handle src, bool) {
        if (src.is_none()) { value = QVariant(); return true; }
        if (PyBool_Check(src.ptr())) { value = (src.ptr() == Py_True); return true; }
        if (PyLong_Check(src.ptr())) { value = (qlonglong)PyLong_AsLongLong(src.ptr()); return true; }
        if (PyFloat_Check(src.ptr())) { value = PyFloat_AsDouble(src.ptr()); return true; }
        if (PyUnicode_Check(src.ptr())) {
            value = QString::fromUtf8(PyUnicode_AsUTF8(src.ptr()));
            return true;
        }
        return false;
    }
    static handle cast(const QVariant &v, return_value_policy, handle) {
        switch (v.typeId()) {
            case QMetaType::Bool:
                return PyBool_FromLong(v.toBool());
            case QMetaType::Int:
            case QMetaType::LongLong:
            case QMetaType::UInt:
            case QMetaType::ULongLong:
                return PyLong_FromLongLong(v.toLongLong());
            case QMetaType::Double:
            case QMetaType::Float:
                return PyFloat_FromDouble(v.toDouble());
            case QMetaType::QString:
                return PyUnicode_FromString(v.toString().toUtf8().constData());
            default:
                Py_RETURN_NONE;
        }
    }
};

// QList<QString> — handled via QStringList caster above; no generic QList<T> template
// (the generic template causes issues with incomplete types in pybind11 descr.h)

} // namespace pybind11::detail
