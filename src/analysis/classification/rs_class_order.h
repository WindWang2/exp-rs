// rs_class_order.h — Classification & Object Intelligence 11.0 (F12).
//
// Single authority for probability column order across every producer of
// class probabilities (OpenCV backends, pipeline outputs, calibration):
//
//   probability column k of an N x K matrix refers to class
//   classIds[k], where classIds is the strictly ascending list of
//   distinct training labels.
//
// Every serialised artifact that carries probabilities must also carry
// (or be resolvable against) this order, so a reloaded model keeps a
// machine-verifiable, stable column mapping (GOAL Oracle 2).
//
// Serialisation format matches the existing per-backend companion files
// (rs_classifier_random_forest.cpp:154-188, rs_classifier_mlp.cpp):
// a bare JSON array of ints, e.g. [1,3,7].
#pragma once

#include "qgis_analysis_export.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

class QGIS_ANALYSIS_EXPORT RsClassOrder
{
  public:
    /// Strictly ascending, deduplicated class ids from raw labels.
    /// Empty input yields an empty vector.
    static QVector<int> sortedClassIds( const QVector<int> &labels );

    /// True when \a classIds is a valid column order: non-empty and
    /// strictly ascending (no duplicates).
    static bool isValid( const QVector<int> &classIds );

    /// True when a probability matrix with \a probColumns columns is
    /// consistent with \a classIds (K must match exactly).
    static bool matchesColumnCount( const QVector<int> &classIds, int probColumns );

    /// Column index of \a classId within \a classIds, or -1 when absent.
    static int columnOf( const QVector<int> &classIds, int classId );

    /// Bare-array serialisation ([1,3,7]) — the companion-file format.
    static QJsonDocument toJson( const QVector<int> &classIds );

    /// Parses a bare array into a column order. Returns false (and clears
    /// \a classIds) when the document is not an array, holds non-integral
    /// values, or is not strictly ascending.
    static bool fromJson( const QJsonDocument &doc, QVector<int> &classIds );

    /// Convenience: parse from a JSON object key (used by the superset
    /// model sidecar's "classOrder" section). Same validation as fromJson().
    static bool fromJsonArray( const QJsonArray &arr, QVector<int> &classIds );

    /// Bare-array form of toJson() for embedding in larger documents.
    static QJsonArray toJsonArray( const QVector<int> &classIds );
};
