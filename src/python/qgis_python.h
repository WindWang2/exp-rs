// src/python/qgis_python.h
#pragma once

#include <QString>

class QgisPython
{
public:
    static QgisPython &instance();

    bool initialize();
    void finalize();

    bool isInitialized() const { return m_initialized; }

    bool runString(const QString &command, QString &output, QString &error);

private:
    QgisPython() = default;
    ~QgisPython() = default;
    QgisPython(const QgisPython &) = delete;
    QgisPython &operator=(const QgisPython &) = delete;

    bool m_initialized = false;
};

void init_python_bindings();
