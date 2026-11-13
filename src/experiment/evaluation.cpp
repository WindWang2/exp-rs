// evaluation.cpp — metric implementations (single-sourced formulas).
#include "evaluation.h"

#include "data/execution_fingerprint.h"

#include <QJsonArray>

#include <QCryptographicHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::experiment
{

namespace
{

double safeDivide( double numerator, double denominator )
{
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

/// R30-T01 (WP01): exact sum of non-negative qint64 cells beyond qint64's
/// range. Two 64-bit limbs keep the arithmetic portable (no __int128 on the
/// MSVC CI lanes) and exact: every stored cell is ≤ 2^53-1 (reader) or
/// INT64_MAX (mutators) and the store holds ≤ kMaxStoredCells cells, so the
/// grand total stays far inside the 128-bit range. Ratios are computed from
/// toDouble() projections (relative error ≤ 2^-52 — far tighter than the
/// metrics' documented tolerances); the qint64 accessors saturate instead of
/// wrapping a signed overflow.
struct WideSum
{
    quint64 lo = 0;
    quint64 hi = 0;

    void add( qint64 value )
    {
        // All stored counts are non-negative (setCount refuses negatives).
        const quint64 v = static_cast<quint64>( value );
        const quint64 before = lo;
        lo += v;
        hi += lo < before ? 1u : 0u;
    }
    bool fitsInt64() const
    {
        return hi == 0 && lo <= static_cast<quint64>( std::numeric_limits<qint64>::max() );
    }
    bool isZero() const { return hi == 0 && lo == 0; }
    double toDouble() const
    {
        // 2^64 = 18446744073709551616 exactly as a double.
        return double( hi ) * 18446744073709551616.0 + double( lo );
    }
    qint64 saturateInt64() const { return fitsInt64() ? qint64( lo ) : std::numeric_limits<qint64>::max(); }
};

} // namespace

// --- ConfusionMatrix -------------------------------------------------------------

qint64 ConfusionMatrix::count( qint64 truthRow, qint64 predictedColumn ) const
{
    // R29-T01 (key alias): the flat key row*size()+column maps out-of-grid
    // reads onto LEGAL cells' keys — with n=2, count(0,2) aliased count(1,0)
    // and returned its stored value instead of 0. Mutations already refuse
    // out-of-grid indices; the read path must agree with them.
    if ( truthRow < 0 || predictedColumn < 0 || truthRow >= size()
         || predictedColumn >= size() )
        return 0;
    return m_counts.value( truthRow * size() + predictedColumn, 0 );
}

void ConfusionMatrix::increment( qint64 truthRow, qint64 predictedColumn )
{
    // R2 review (P3): the sparse key is row*size()+column, so an out-of-grid
    // index decodes to row/column outside [0,size()) and matrixSums would
    // write past the end of the totals vectors (heap write on a read path).
    // Refuse the mutation instead — the dense store used to surface the same
    // mistake through QVector::at()'s bounds check.
    if ( truthRow < 0 || predictedColumn < 0 || truthRow >= size()
         || predictedColumn >= size() )
        return;
    const qint64 key = truthRow * size() + predictedColumn;
    // R29-T01 (increment extreme): ++ at qint64 max is signed overflow
    // (wraps negative in practice) — a corrupted total, never a count.
    // Saturate in place: the cell already holds the largest count qint64
    // can express.
    const auto it = m_counts.constFind( key );
    if ( it != m_counts.constEnd() && it.value() == std::numeric_limits<qint64>::max() )
        return;
    ++m_counts[key];
}

void ConfusionMatrix::setCount( qint64 truthRow, qint64 predictedColumn, qint64 value )
{
    // R2 review (P3): same out-of-grid guard as increment().
    if ( truthRow < 0 || predictedColumn < 0 || truthRow >= size()
         || predictedColumn >= size() )
        return;
    // R29-T01 (negative setCount): a negative cell would silently flip the
    // sign of every downstream total (row/column sums, grand total) — the
    // read side (fromJson) refuses negative counts, the mutation side must
    // hold the same contract.
    if ( value < 0 )
        return;
    const qint64 key = truthRow * size() + predictedColumn;
    if ( value == 0 )
        m_counts.remove( key ); // keep the sparse store sparse
    else
        m_counts.insert( key, value );
}

namespace
{
/// One-pass accumulator over the sparse store: per-row / per-column totals,
/// diagonal cells and the grand total in O(nonzero cells + labels).
/// R30-T01 (WP01): totals accumulate in WideSum (exact 128-bit) — cells are
/// individually legal, but 1025 cells of 2^53-1 overflow the qint64 grand
/// total and the wrapped value used to poison every score downstream.
struct MatrixSums
{
    QVector<WideSum> rowTotals;
    QVector<WideSum> colTotals;
    QVector<WideSum> diagonal;
    WideSum grand;
    /// True when ANY qint64-exposed total (grand, a row or a column) left
    /// the qint64 range — see ConfusionMatrix::totalsSaturated().
    bool saturated = false;

    WideSum diagonalTotal() const
    {
        WideSum sum;
        for ( const WideSum &value : diagonal )
        {
            const WideSum before = sum;
            sum.lo += value.lo;
            sum.hi += value.hi;
            if ( sum.lo < before.lo )
                ++sum.hi; // carry from the low limbs
        }
        return sum;
    }
};

MatrixSums matrixSums( const QStringList &labels, const QHash<qint64, qint64> &counts )
{
    const qint64 n = labels.size();
    MatrixSums sums;
    sums.rowTotals = QVector<WideSum>( int( n ) );
    sums.colTotals = QVector<WideSum>( int( n ) );
    sums.diagonal = QVector<WideSum>( int( n ) );
    for ( auto it = counts.constBegin(); it != counts.constEnd(); ++it )
    {
        const qint64 row = it.key() / n;
        const qint64 column = it.key() % n;
        sums.rowTotals[int( row )].add( it.value() );
        sums.colTotals[int( column )].add( it.value() );
        sums.grand.add( it.value() );
        if ( row == column )
            sums.diagonal[int( row )].add( it.value() );
    }
    sums.saturated = !sums.grand.fitsInt64();
    for ( qint64 i = 0; i < n && !sums.saturated; ++i )
        sums.saturated = !sums.rowTotals.at( int( i ) ).fitsInt64()
                         || !sums.colTotals.at( int( i ) ).fitsInt64();
    return sums;
}

// --- single-sourced per-class formulas (evaluation.h: "implemented exactly
// once") — every public metric and toJson() derives through these helpers so
// the aggregation refactor cannot drift the numbers. -----------------------

double classPrecision( const MatrixSums &sums, qint64 index )
{
    return safeDivide( sums.diagonal.at( int( index ) ).toDouble(),
                       sums.colTotals.at( int( index ) ).toDouble() );
}

double classRecall( const MatrixSums &sums, qint64 index )
{
    return safeDivide( sums.diagonal.at( int( index ) ).toDouble(),
                       sums.rowTotals.at( int( index ) ).toDouble() );
}

double f1From( double precision, double recall )
{
    return precision + recall > 0.0 ? 2.0 * precision * recall / ( precision + recall ) : 0.0;
}

double classIou( const MatrixSums &sums, qint64 index )
{
    const double tp = sums.diagonal.at( int( index ) ).toDouble();
    return safeDivide( tp, sums.colTotals.at( int( index ) ).toDouble()
                                 + sums.rowTotals.at( int( index ) ).toDouble() - tp );
}

ConfusionMatrix::PerClassMetrics classMetrics( const QStringList &labels, const MatrixSums &sums,
                                               qint64 index )
{
    ConfusionMatrix::PerClassMetrics metrics;
    metrics.label = labels.at( int( index ) );
    const WideSum &rowTotal = sums.rowTotals.at( int( index ) );
    // R30-T01 (WP01): the qint64 projection saturates past INT64_MAX; the
    // flag keeps it distinguishable from a genuine INT64_MAX support.
    metrics.support = rowTotal.saturateInt64();
    metrics.supportSaturated = !rowTotal.fitsInt64();
    // R29-T01 (WP09): a zero-support class has an UNDEFINED per-class
    // metric (zero denominator → the documented 0 policy) — mark it so
    // consumers can tell "no samples" from "classified badly".
    metrics.zeroSupport = rowTotal.isZero();
    metrics.precision = classPrecision( sums, index );
    metrics.recall = classRecall( sums, index );
    metrics.f1 = f1From( metrics.precision, metrics.recall );
    metrics.iou = classIou( sums, index );
    return metrics;
}

/// Gorodkin's multivariate R_K from precomputed sums (see ConfusionMatrix::mcc).
double mccFromSums( const MatrixSums &sums, qint64 size )
{
    const double s = sums.grand.toDouble();
    if ( s <= 0.0 )
        return 0.0;
    const double c = sums.diagonalTotal().toDouble();
    double sumP2 = 0.0;
    double sumT2 = 0.0;
    double sumPT = 0.0;
    for ( qint64 k = 0; k < size; ++k )
    {
        const double t = sums.rowTotals.at( int( k ) ).toDouble();
        const double p = sums.colTotals.at( int( k ) ).toDouble();
        sumP2 += p * p;
        sumT2 += t * t;
        sumPT += p * t;
    }
    const double covariance = c * s - sumPT;
    const double denominator = std::sqrt( ( s * s - sumP2 ) * ( s * s - sumT2 ) );
    return denominator > 0.0 ? covariance / denominator : 0.0;
}

/// Macro mean over classes with support (either side), from precomputed sums —
/// F1 flavor when @p iou is false, IoU flavor otherwise (macroF1/macroIoU).
double macroAverageFromSums( const MatrixSums &sums, qint64 size, bool iou )
{
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size; ++i )
    {
        if ( sums.rowTotals.at( int( i ) ).isZero() && sums.colTotals.at( int( i ) ).isZero() )
            continue;
        sum += iou ? classIou( sums, i )
                   : f1From( classPrecision( sums, i ), classRecall( sums, i ) );
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}
} // namespace

qint64 ConfusionMatrix::total() const
{
    // R29-T01 (WP03): the grand total needs no per-label vectors — sum the
    // stored cells directly (allocation-free, no key decode).
    // R30-T01 (WP01): the sum saturates instead of wrapping when the
    // mathematical total leaves qint64 (see totalsSaturated()).
    WideSum sum;
    for ( auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it )
        sum.add( it.value() );
    return sum.saturateInt64();
}

bool ConfusionMatrix::totalsSaturated() const
{
    return matrixSums( m_labels, m_counts ).saturated;
}

qint64 ConfusionMatrix::truthTotal( qint64 row ) const
{
    WideSum sum;
    for ( qint64 column = 0; column < size(); ++column )
        sum.add( count( row, column ) );
    return sum.saturateInt64();
}

qint64 ConfusionMatrix::predictedTotal( qint64 column ) const
{
    WideSum sum;
    for ( qint64 row = 0; row < size(); ++row )
        sum.add( count( row, column ) );
    return sum.saturateInt64();
}

double ConfusionMatrix::overallAccuracy() const
{
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    return safeDivide( sums.diagonalTotal().toDouble(), sums.grand.toDouble() );
}

double ConfusionMatrix::balancedAccuracy() const
{
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    qint64 classesWithSupport = 0;
    double recallSum = 0.0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( sums.rowTotals.at( int( i ) ).isZero() )
            continue;
        ++classesWithSupport;
        recallSum += classRecall( sums, i );
    }
    return classesWithSupport > 0 ? recallSum / double( classesWithSupport ) : 0.0;
}

ConfusionMatrix::PerClassMetrics ConfusionMatrix::perClass( qint64 index ) const
{
    PerClassMetrics metrics;
    if ( index < 0 || index >= size() )
        return metrics;
    return classMetrics( m_labels, matrixSums( m_labels, m_counts ), index );
}

QVector<ConfusionMatrix::PerClassMetrics> ConfusionMatrix::perClassAll() const
{
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    QVector<PerClassMetrics> all;
    all.reserve( int( size() ) );
    for ( qint64 i = 0; i < size(); ++i )
        all.append( classMetrics( m_labels, sums, i ) );
    return all;
}

double ConfusionMatrix::macroPrecision() const
{
    // R29-T01 (WP03): one aggregation pass per call (the old body ran
    // perClassAll + matrixSums — two traversals of the sparse store).
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( sums.colTotals.at( int( i ) ).isZero() )
            continue;
        sum += classPrecision( sums, i );
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}

double ConfusionMatrix::macroRecall() const
{
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    double sum = 0.0;
    qint64 classes = 0;
    for ( qint64 i = 0; i < size(); ++i )
    {
        if ( sums.rowTotals.at( int( i ) ).isZero() )
            continue;
        sum += classRecall( sums, i );
        ++classes;
    }
    return classes > 0 ? sum / double( classes ) : 0.0;
}

double ConfusionMatrix::macroF1() const
{
    return macroAverageFromSums( matrixSums( m_labels, m_counts ), size(), false );
}

double ConfusionMatrix::macroIoU() const
{
    return macroAverageFromSums( matrixSums( m_labels, m_counts ), size(), true );
}

double ConfusionMatrix::weightedF1() const
{
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    if ( sums.grand.isZero() )
        return 0.0;
    double weighted = 0.0;
    for ( qint64 i = 0; i < size(); ++i )
        weighted += sums.rowTotals.at( int( i ) ).toDouble()
                    * f1From( classPrecision( sums, i ), classRecall( sums, i ) );
    return weighted / sums.grand.toDouble();
}

double ConfusionMatrix::kappa() const
{
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    const double grand = sums.grand.toDouble();
    if ( grand <= 0.0 )
        return 0.0;
    const double observed = safeDivide( sums.diagonalTotal().toDouble(), grand );
    double expected = 0.0;
    for ( qint64 i = 0; i < size(); ++i )
        expected += sums.rowTotals.at( int( i ) ).toDouble()
                    * sums.colTotals.at( int( i ) ).toDouble();
    expected /= grand * grand;
    return expected < 1.0 ? ( observed - expected ) / ( 1.0 - expected ) : 0.0;
}

double ConfusionMatrix::mcc() const
{
    // Gorodkin's multivariate R_K: covariance formulation over the k×k
    // matrix. Empty matrices report 0, never NaN.
    return mccFromSums( matrixSums( m_labels, m_counts ), size() );
}

QJsonObject ConfusionMatrix::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( m_labels ) );
    // #1804 (WE-25): dense counts are only emitted while the square stays
    // small; beyond the threshold the emission switches to nonzero triplets
    // (counts_format="sparse") so serialization memory stays proportional to
    // recorded cells, not to labels^2. fromJson accepts both formats.
    constexpr qint64 kDenseEmitLabels = 1024;
    if ( size() <= kDenseEmitLabels )
    {
        QJsonArray rows;
        for ( qint64 row = 0; row < size(); ++row )
        {
            QJsonArray cells;
            for ( qint64 column = 0; column < size(); ++column )
                cells.append( count( row, column ) );
            rows.append( cells );
        }
        json.insert( QStringLiteral( "counts" ), rows );
    }
    else
    {
        // R1 review (P2-7): emit nonzero cells in ASCENDING KEY order. QHash
        // iteration order is per-process randomized, so an unsorted emission
        // serialized the same logical matrix to different bytes on every
        // run — poisoning byte-comparison and content-hash consumers that
        // assume same-matrix ⇒ same-bytes.
        QList<qint64> keys;
        keys.reserve( m_counts.size() );
        for ( auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it )
            keys.append( it.key() );
        std::sort( keys.begin(), keys.end() );
        QJsonArray nonZero;
        const qint64 n = size();
        for ( const qint64 key : keys )
        {
            QJsonObject cell;
            cell.insert( QStringLiteral( "r" ), key / n );
            cell.insert( QStringLiteral( "c" ), key % n );
            cell.insert( QStringLiteral( "n" ), m_counts.value( key ) );
            nonZero.append( cell );
        }
        json.insert( QStringLiteral( "counts_format" ), QStringLiteral( "sparse" ) );
        json.insert( QStringLiteral( "nonzero_counts" ), nonZero );
    }
    // R29-T01 (WP03): one matrixSums pass feeds every aggregate in this
    // document — the old body ran one-to-two traversals PER metric call
    // (≈11 total over the same sparse store).
    const MatrixSums sums = matrixSums( m_labels, m_counts );
    // R30-T01 (WP01): additive saturation marker — present only when a
    // qint64-exposed total (grand/row/column) left the qint64 range; scores
    // above still come from the exact wide sums.
    if ( sums.saturated )
        json.insert( QStringLiteral( "totals_saturated" ), true );
    json.insert( QStringLiteral( "overall_accuracy" ),
                 safeDivide( sums.diagonalTotal().toDouble(), sums.grand.toDouble() ) );
    {
        qint64 classesWithSupport = 0;
        double recallSum = 0.0;
        for ( qint64 i = 0; i < size(); ++i )
        {
            if ( sums.rowTotals.at( int( i ) ).isZero() )
                continue;
            ++classesWithSupport;
            recallSum += classRecall( sums, i );
        }
        json.insert( QStringLiteral( "balanced_accuracy" ),
                     classesWithSupport > 0 ? recallSum / double( classesWithSupport ) : 0.0 );
    }
    {
        const double grand = sums.grand.toDouble();
        const double observed = safeDivide( sums.diagonalTotal().toDouble(), grand );
        double expected = 0.0;
        for ( qint64 i = 0; i < size(); ++i )
            expected += sums.rowTotals.at( int( i ) ).toDouble()
                        * sums.colTotals.at( int( i ) ).toDouble();
        expected /= grand * grand;
        json.insert( QStringLiteral( "kappa" ),
                     grand > 0.0 && expected < 1.0 ? ( observed - expected ) / ( 1.0 - expected )
                                                   : 0.0 );
    }
    json.insert( QStringLiteral( "mcc" ), mccFromSums( sums, size() ) );
    json.insert( QStringLiteral( "macro_f1" ), macroAverageFromSums( sums, size(), false ) );
    json.insert( QStringLiteral( "macro_iou" ), macroAverageFromSums( sums, size(), true ) );
    {
        double weighted = 0.0;
        for ( qint64 i = 0; i < size(); ++i )
            weighted += sums.rowTotals.at( int( i ) ).toDouble()
                        * f1From( classPrecision( sums, i ), classRecall( sums, i ) );
        json.insert( QStringLiteral( "weighted_f1" ),
                     sums.grand.isZero() ? 0.0 : weighted / sums.grand.toDouble() );
    }
    QJsonArray perClassArray;
    for ( qint64 i = 0; i < size(); ++i )
    {
        const PerClassMetrics metrics = classMetrics( m_labels, sums, i );
        QJsonObject item;
        item.insert( QStringLiteral( "label" ), metrics.label );
        item.insert( QStringLiteral( "precision" ), metrics.precision );
        item.insert( QStringLiteral( "recall" ), metrics.recall );
        item.insert( QStringLiteral( "f1" ), metrics.f1 );
        item.insert( QStringLiteral( "iou" ), metrics.iou );
        item.insert( QStringLiteral( "support" ), qint64( metrics.support ) );
        // R29-T01 (WP09): optional marker — present only for zero-support
        // classes (absent = normally supported; old readers ignore it).
        if ( metrics.zeroSupport )
            item.insert( QStringLiteral( "zero_support" ), true );
        // R30-T01 (WP01): optional marker — present only when the qint64
        // support projection saturated.
        if ( metrics.supportSaturated )
            item.insert( QStringLiteral( "support_saturated" ), true );
        perClassArray.append( item );
    }
    json.insert( QStringLiteral( "per_class" ), perClassArray );
    return json;
}

Result<ConfusionMatrix> ConfusionMatrix::fromJson( const QJsonObject &json )
{
    using ResultT = Result<ConfusionMatrix>;
    ConfusionMatrix matrix;
    // R30-T01 (WP02): labels are the schema class CODES — the emitter always
    // writes strings. Reading through toVariant().toStringList() silently
    // stringified non-string entries (7 became "7"), reinterpreting a torn
    // document instead of refusing it.
    {
        const QJsonValue labelsValue = json.value( QStringLiteral( "labels" ) );
        if ( !labelsValue.isArray() )
        {
            return ResultT::failure( Diagnostic{
                QStringLiteral( "evaluation.invalid" ),
                QStringLiteral( "confusion matrix 'labels' must be an array of class codes" ),
                DiagnosticSeverity::Error } );
        }
        const QJsonArray labelsArray = labelsValue.toArray();
        for ( const auto &entry : labelsArray )
        {
            if ( !entry.isString() )
            {
                return ResultT::failure( Diagnostic{
                    QStringLiteral( "evaluation.invalid" ),
                    QStringLiteral( "confusion matrix 'labels' entries must be strings "
                                    "(class codes); found a non-string entry" ),
                    DiagnosticSeverity::Error } );
            }
            matrix.m_labels.append( entry.toString() );
        }
    }
    if ( matrix.m_labels.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "confusion matrix needs labels" ),
                                             DiagnosticSeverity::Error } );
    }
    // #1804 (WE-25): the label cap is a sanity bound on the label LIST (and
    // keeps the flat row*size()+col key comfortably inside qint64), not a
    // memory device anymore: counts are stored sparsely below, so a
    // legitimate 8192-class taxonomy no longer implies a 512 MB dense
    // allocation — and no longer gets refused for one.
    constexpr qint64 kMaxConfusionLabels = 65536;
    if ( matrix.size() > kMaxConfusionLabels )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "evaluation.invalid" ),
            QStringLiteral( "confusion matrix refuses %1 labels (cap %2)" )
                .arg( matrix.size() )
                .arg( kMaxConfusionLabels ),
            DiagnosticSeverity::Error } );
    }
    // R29-T01 (duplicate labels): identity is BY CODE ("labels are the schema
    // class codes … identity by CODE, never by row index") — two identical
    // codes would double-report one class and skew every total. The emitter
    // never produces duplicates; a duplicate means a hand-edited or torn
    // document, which is refused rather than reinterpreted.
    {
        QSet<QString> seen;
        seen.reserve( matrix.m_labels.size() );
        for ( const QString &label : matrix.m_labels )
        {
            if ( !seen.contains( label ) )
                seen.insert( label );
            else
                return ResultT::failure( Diagnostic{
                    QStringLiteral( "evaluation.invalid" ),
                    QStringLiteral( "confusion matrix labels contain the duplicate code '%1'" )
                        .arg( label ),
                    DiagnosticSeverity::Error } );
        }
    }
    // R29-T01 (mixed format): a document carrying BOTH the dense `counts`
    // grid and the sparse `nonzero_counts` triplets is ambiguous (the sparse
    // branch used to win silently, dropping the dense payload). Refuse —
    // the emitter writes exactly one of the two.
    const bool hasSparseCells = json.contains( QStringLiteral( "nonzero_counts" ) );
    const bool hasDenseCells = json.contains( QStringLiteral( "counts" ) );
    const bool declaresSparse =
        json.value( QStringLiteral( "counts_format" ) ).toString() == QLatin1String( "sparse" );
    if ( hasSparseCells && hasDenseCells )
        return ResultT::failure( Diagnostic{
            QStringLiteral( "evaluation.invalid" ),
            QStringLiteral( "confusion matrix carries both 'counts' and 'nonzero_counts' — "
                            "mixed dense/sparse format is ambiguous; provide exactly one" ),
            DiagnosticSeverity::Error } );
    // R29-T01 (unknown format token): counts_format is the format's own
    // declaration; an unrecognized token describes a layout this reader does
    // not know, which must refuse instead of guessing (readers refuse
    // foreign versions rather than silently reinterpreting — layout contract).
    if ( json.contains( QStringLiteral( "counts_format" ) )
         && json.value( QStringLiteral( "counts_format" ) ).toString()
              != QLatin1String( "sparse" ) )
        return ResultT::failure( Diagnostic{
            QStringLiteral( "evaluation.invalid" ),
            QStringLiteral( "confusion matrix counts_format '%1' is not supported (expected "
                            "'sparse')" )
                .arg( json.value( QStringLiteral( "counts_format" ) ).toString() ),
            DiagnosticSeverity::Error } );
    // R30-T01 (WP02): a document that DECLARES the sparse layout but carries
    // no nonzero_counts payload contradicts itself — reading the dense grid
    // behind a sparse declaration would silently reinterpret it.
    if ( declaresSparse && !hasSparseCells )
        return ResultT::failure( Diagnostic{
            QStringLiteral( "evaluation.invalid" ),
            QStringLiteral( "confusion matrix declares counts_format='sparse' but carries no "
                            "'nonzero_counts' array" ),
            DiagnosticSeverity::Error } );
    // Declared resource budgets (R30-T01 WP02): nonzero cells actually stored
    // (the explicit bound that replaced the old labels^2 allocation) and ALL
    // declared sparse entries. The entries bound is what keeps a zero-heavy
    // or torn payload from looping unbounded through the reader — zeros are
    // legal entries but never enter the store, so the nonzero budget alone
    // could not see them.
    constexpr qint64 kMaxStoredCells = ConfusionMatrix::kMaxStoredCells;
    constexpr qint64 kMaxSparseEntries = ConfusionMatrix::kMaxSparseEntries;
    // R29-T01 (integer precision): JSON numbers are IEEE doubles. Above 2^53
    // the double grid is coarser than 1, so a count literal cannot round-trip
    // exactly through QJsonValue — the reader would silently store a
    // neighboring integer. Refuse at the exactness boundary instead (the
    // historical 2^63 bound only guarded the qint64 cast, not the
    // representation).
    constexpr double kExactCountLimit = 9007199254740992.0; // 2^53
    qint64 storedCells = 0;
    const auto storeCell = [&]( qint64 row, qint64 column, qint64 value ) -> bool {
        if ( value == 0 )
            return true; // zero cells stay unrepresented
        if ( storedCells >= kMaxStoredCells )
            return false;
        matrix.setCount( row, column, value );
        ++storedCells;
        return true;
    };
    const auto cellDiagnostic = [&]( const QString &detail ) {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ), detail,
                                             DiagnosticSeverity::Error } );
    };
    if ( hasSparseCells )
    {
        // R30-T01 (WP02): a present-but-non-array payload used to read as an
        // empty array — a torn export parsed as a SUCCESSFUL empty matrix.
        const QJsonValue sparseValue = json.value( QStringLiteral( "nonzero_counts" ) );
        if ( !sparseValue.isArray() )
        {
            return cellDiagnostic( QStringLiteral( "confusion matrix 'nonzero_counts' must be "
                                                   "an array of {r,c,n} cells (found a "
                                                   "non-array value)" ) );
        }
        // Sparse format emitted by toJson() above.
        const QJsonArray cells = sparseValue.toArray();
        if ( cells.size() > kMaxSparseEntries )
        {
            return cellDiagnostic(
                QStringLiteral( "confusion matrix 'nonzero_counts' declares %1 entries "
                                "(budget %2); the budget bounds ALL declared entries — "
                                "zero-valued included — so torn or padded payloads are "
                                "refused up front (the QJsonDocument parse memory that "
                                "produced them is upstream and remains the caller's)" )
                    .arg( cells.size() )
                    .arg( kMaxSparseEntries ) );
        }
        QSet<qint64> sparseSeen;
        sparseSeen.reserve( cells.size() );
        for ( const auto &entry : cells )
        {
            const QJsonObject cell = entry.toObject();
            // R2 review (P3): a fractional r/c silently reindexed the cell
            // (toInteger truncates 1.5 → 1). Read as a double, require an
            // exact integer, and range-check — the same discipline the dense
            // path applies to counts.
            const auto gridIndex = [&]( const QString &key, qint64 &out ) -> bool {
                const QJsonValue value = cell.value( key );
                if ( !value.isDouble() )
                    return false;
                const double raw = value.toDouble();
                if ( !std::isfinite( raw ) || std::floor( raw ) != raw
                     || raw < 0.0 || raw >= 9223372036854775808.0 )
                    return false;
                out = qint64( raw );
                return true;
            };
            qint64 row = -1;
            qint64 column = -1;
            if ( !gridIndex( QStringLiteral( "r" ), row )
                 || !gridIndex( QStringLiteral( "c" ), column )
                 || row >= matrix.size() || column >= matrix.size() )
            {
                return cellDiagnostic( QStringLiteral( "nonzero_counts cell coordinates must be"
                                                       " integral indices inside the label grid" ) );
            }
            const QJsonValue raw = cell.value( QStringLiteral( "n" ) );
            if ( !raw.isDouble() )
            {
                return cellDiagnostic( QStringLiteral( "nonzero_counts cell 'n' is not a number" ) );
            }
            const double rawDouble = raw.toDouble();
            // R1 review (P1-1/P2-4): mirror the dense path's guards —
            // negative counts corrupt every downstream total. R29-T01: the
            // bound is the double-precision exactness limit 2^53 (see
            // kExactCountLimit above), which is strictly tighter than the
            // qint64 cast boundary and certifies the stored count equals the
            // document's literal.
            if ( !std::isfinite( rawDouble ) || std::floor( rawDouble ) != rawDouble ||
                 rawDouble < 0.0 || rawDouble >= kExactCountLimit )
            {
                return cellDiagnostic(
                    QStringLiteral( "nonzero_counts cell 'n' must be a finite non-negative integer "
                                    "below 2^53 (JSON doubles cannot certify larger counts exactly)" ) );
            }
            // R29-T01 (duplicate cells): each (r,c) appears at most once in
            // an emitted document; a duplicate is torn/hand-edited and its
            // two values are ambiguous — the old reader silently kept the
            // LAST one. Tracked in an independent seen-set: a stored count
            // is absent for n==0 cells (storeCell skips zeros), so the
            // store itself cannot detect a "0 then positive" duplicate
            // (R1 review P3).
            const qint64 sparseKey = row * matrix.size() + column;
            if ( sparseSeen.contains( sparseKey ) )
            {
                return cellDiagnostic(
                    QStringLiteral( "nonzero_counts declares cell (%1,%2) twice — each grid "
                                    "position may appear at most once" )
                        .arg( row )
                        .arg( column ) );
            }
            sparseSeen.insert( sparseKey );
            if ( !storeCell( row, column, qint64( rawDouble ) ) )
            {
                return cellDiagnostic( QStringLiteral( "confusion matrix refuses more than %1 nonzero cells" )
                                           .arg( kMaxStoredCells ) );
            }
        }
        return ResultT::success( matrix );
    }
    const QJsonArray rows = json.value( QStringLiteral( "counts" ) ).toArray();
    if ( rows.size() != int( matrix.size() ) )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "counts rows != label count" ),
                                             DiagnosticSeverity::Error } );
    }
    for ( int row = 0; row < rows.size(); ++row )
    {
        const QJsonArray cells = rows.at( row ).toArray();
        if ( cells.size() != int( matrix.size() ) )
        {
            return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                                 QStringLiteral( "counts row width mismatch" ),
                                                 DiagnosticSeverity::Error } );
        }
        for ( int column = 0; column < cells.size(); ++column )
        {
            // #1782 (WE-23): QJsonValue::toInteger() silently maps strings,
            // nulls and fractional cells to 0 — a torn export then read as a
            // perfect diagonal-leaning matrix. Only genuine integral numbers
            // are accepted; everything else refuses with the cell address.
            const QJsonValue cell = cells.at( column );
            if ( !cell.isDouble() )
            {
                return ResultT::failure( Diagnostic{
                    QStringLiteral( "evaluation.invalid" ),
                    QStringLiteral( "counts[%1][%2] is not a number" ).arg( row ).arg( column ),
                    DiagnosticSeverity::Error } );
            }
            const double raw = cell.toDouble();
            // R1-F1: the cast below is only defined inside qint64's range —
            // a finite huge literal (e.g. 1e30) must refuse, not wrap.
            // R1 review (P2-4): the bound is EXCLUSIVE of 2^63 — as doubles,
            // 9223372036854775807.0 == 2^63 exactly, so `> kMaxCount` still
            // accepted 2^63 and qint64(2^63) wrapped to INT64_MIN.
            // R29-T01: tightened to the double-precision exactness limit 2^53
            // (same rationale as the sparse path — see kExactCountLimit).
            if ( !std::isfinite( raw ) || std::floor( raw ) != raw || raw < 0.0
                 || raw >= kExactCountLimit )
            {
                return ResultT::failure( Diagnostic{
                    QStringLiteral( "evaluation.invalid" ),
                    QStringLiteral( "counts[%1][%2] must be a finite non-negative integer below "
                                    "2^53 (got %3)" )
                        .arg( row )
                        .arg( column )
                        .arg( raw ),
                    DiagnosticSeverity::Error } );
            }
            const qint64 value = qint64( raw );
            if ( !storeCell( row, column, value ) )
            {
                return ResultT::failure( Diagnostic{
                    QStringLiteral( "evaluation.invalid" ),
                    QStringLiteral( "confusion matrix refuses more than %1 nonzero cells" )
                        .arg( kMaxStoredCells ),
                    DiagnosticSeverity::Error } );
            }
        }
    }
    return ResultT::success( matrix );
}

// --- Regression ---------------------------------------------------------------------

namespace
{
/// R30-T01 (WP03): Neumaier-compensated accumulator in long double. Doubles
/// are exactly representable in the 64-bit mantissa, so a difference of two
/// doubles never overflows before the final projection; compensation kills
/// the catastrophic cancellation the plain sequential double sum showed on
/// mixed-magnitude inputs (1e16 + 1 + 1 - 1e16 summed to 0.0 instead of 2).
struct StableSum
{
    long double sum = 0.0L;
    long double compensation = 0.0L;

    void add( long double value )
    {
        const long double t = sum + value;
        if ( std::fabs( sum ) >= std::fabs( value ) )
            compensation += ( sum - t ) + value;
        else
            compensation += ( value - t ) + sum;
        sum = t;
    }
    long double value() const { return sum + compensation; }
};

/// Project a long-double result onto the metric contract: a FINITE input
/// pair set whose true value exceeds double range is a distinct outcome from
/// non-finite inputs — the metric holds ±inf in memory (honest signal),
/// serializes as null (what QJsonValue already did with inf) and flips the
/// document-level `representable` marker.
double projectMetric( long double wide, bool *representable )
{
    if ( std::isfinite( wide ) && std::fabs( wide ) <= static_cast<long double>( std::numeric_limits<double>::max() ) )
        return static_cast<double>( wide );
    *representable = false;
    return wide > 0.0L ? std::numeric_limits<double>::infinity()
                       : -std::numeric_limits<double>::infinity();
}
} // namespace

Result<RegressionMetrics> regressionMetrics( const QVector<double> &truth,
                                             const QVector<double> &predicted )
{
    using ResultT = Result<RegressionMetrics>;
    if ( truth.size() != predicted.size() || truth.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "truth/prediction length mismatch" ),
                                             DiagnosticSeverity::Error } );
    }
    RegressionMetrics metrics;
    // #1781 (WE-22): NaN/Inf samples (no-data pixels, torn tensors) used to
    // poison every mean into NaN. Pairs with a non-finite side are skipped
    // AND counted; if nothing finite remains the evaluation refuses rather
    // than emitting NaN metrics that read as success.
    //
    // R30-T01 (WP03): one pass over the input, no finiteTruth/finitePredicted
    // copies (the copies doubled peak memory for wide evaluations). All sums
    // run in long double with Neumaier compensation; the truth mean/variance
    // uses Welford's online algorithm (no second storage pass).
    StableSum absoluteSum;
    StableSum squaredSum;
    StableSum signedSum;
    StableSum percentageSum;
    qint64 mapeCount = 0;
    qint64 finiteCount = 0;
    long double welfordMean = 0.0L;
    long double welfordM2 = 0.0L;
    for ( int i = 0; i < truth.size(); ++i )
    {
        const double t = truth.at( i );
        const double p = predicted.at( i );
        if ( !std::isfinite( t ) || !std::isfinite( p ) )
        {
            ++metrics.nonFiniteSkipped;
            continue;
        }
        // Long double holds every double exactly, so the difference of two
        // FINITE doubles cannot overflow here even when |p - t| > DBL_MAX.
        const long double difference = static_cast<long double>( p ) - static_cast<long double>( t );
        absoluteSum.add( std::fabs( difference ) );
        squaredSum.add( difference * difference );
        signedSum.add( difference );
        if ( t != 0.0 )
        {
            percentageSum.add( std::fabs( difference / static_cast<long double>( t ) ) );
            ++mapeCount;
        }
        else
        {
            ++metrics.mapeSkippedZeros;
        }
        // Welford on the truth series.
        ++finiteCount;
        const long double delta = static_cast<long double>( t ) - welfordMean;
        welfordMean += delta / static_cast<long double>( finiteCount );
        welfordM2 += delta * ( static_cast<long double>( t ) - welfordMean );
    }
    if ( finiteCount == 0 )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "regression metrics: no finite truth/prediction pairs (%1 skipped)" )
                                                 .arg( metrics.nonFiniteSkipped ),
                                             DiagnosticSeverity::Error } );
    }
    metrics.count = finiteCount;
    const long double n = static_cast<long double>( metrics.count );
    metrics.mae = projectMetric( absoluteSum.value() / n, &metrics.representable );
    const long double mseWide = squaredSum.value() / n;
    metrics.mse = projectMetric( mseWide, &metrics.representable );
    // RMSE is the root of the MEAN square — representable even when the mean
    // square itself is not (±1e200 errors: MSE = 1e400, RMSE = 1e200).
    metrics.rmse = projectMetric( std::sqrt( mseWide ), &metrics.representable );
    metrics.bias = projectMetric( signedSum.value() / n, &metrics.representable );
    // #1804 (WE-24): when every truth value is zero MAPE has no defined
    // value — report it as explicitly undefined instead of a clean-looking 0.
    metrics.mapeDefined = mapeCount > 0;
    metrics.mape = metrics.mapeDefined
                       ? projectMetric( 100.0L * percentageSum.value()
                                            / static_cast<long double>( mapeCount ),
                                        &metrics.representable )
                       : 0.0;

    // R² = 1 - SSE/SST; a constant-truth series has no variance → 0 by
    // policy (not NaN, not ±inf).
    const long double sst = welfordM2; // population SST around the compensated mean
    const long double sse = squaredSum.value();
    metrics.r2 = sst > 0.0L ? projectMetric( 1.0L - sse / sst, &metrics.representable ) : 0.0;
    return ResultT::success( metrics );
}

QJsonObject RegressionMetrics::toJson() const
{
    QJsonObject json;
    // R30-T01 (WP03): over-range metrics serialize as null (what QJsonValue
    // already produced for inf) plus the explicit marker — a null under
    // success used to be indistinguishable from a torn document.
    const auto metricValue = []( double v ) {
        return std::isfinite( v ) ? QJsonValue( v ) : QJsonValue( QJsonValue::Null );
    };
    json.insert( QStringLiteral( "mae" ), metricValue( mae ) );
    json.insert( QStringLiteral( "mse" ), metricValue( mse ) );
    json.insert( QStringLiteral( "rmse" ), metricValue( rmse ) );
    json.insert( QStringLiteral( "r2" ), metricValue( r2 ) );
    json.insert( QStringLiteral( "bias" ), metricValue( bias ) );
    // #1804 (WE-24): undefined MAPE is emitted as JSON null plus an explicit
    // mape_defined=false marker — a scalar 0 would read as "perfectly
    // predicted" to every downstream consumer.
    json.insert( QStringLiteral( "mape" ),
                 mapeDefined ? metricValue( mape ) : QJsonValue( QJsonValue::Null ) );
    json.insert( QStringLiteral( "mape_defined" ), mapeDefined );
    json.insert( QStringLiteral( "mape_skipped_zeros" ), mapeSkippedZeros );
    json.insert( QStringLiteral( "non_finite_skipped" ), nonFiniteSkipped );
    json.insert( QStringLiteral( "count" ), count );
    if ( !representable )
        json.insert( QStringLiteral( "representable" ), false );
    return json;
}

// --- Boundary F-score ------------------------------------------------------------------

Result<double> boundaryFScore( const QVector<qint64> &truthBoundary,
                               const QVector<qint64> &predictedBoundary, qint64 imageWidth,
                               double tolerancePx )
{
    using ResultT = Result<double>;
    if ( imageWidth <= 0 || tolerancePx < 0.0 )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "bad boundary inputs" ),
                                             DiagnosticSeverity::Error } );
    }
    if ( truthBoundary.isEmpty() && predictedBoundary.isEmpty() )
        return ResultT::success( 1.0 ); // both empty = identical (no boundary)
    if ( truthBoundary.isEmpty() || predictedBoundary.isEmpty() )
        return ResultT::success( 0.0 );

    const int radius = int( std::ceil( tolerancePx ) );
    const qint64 cellSize = qMax<qint64>( 1, radius );
    const double tolerance2 = tolerancePx * tolerancePx;
    auto buildCells = [&]( const QVector<qint64> &boundary ) {
        QHash<qint64, QVector<qint64>> cells;
        for ( const qint64 index : boundary )
        {
            const qint64 x = index % imageWidth;
            const qint64 y = index / imageWidth;
            cells[( y / cellSize ) * 1000003 + ( x / cellSize )].append( index );
        }
        return cells;
    };
    // A pixel matches when some bucketed pixel of the other boundary lies
    // within tolerance; the 3x3 scan covers every cell the tolerance can reach.
    auto nearInCells = [&]( const QHash<qint64, QVector<qint64>> &cells, qint64 index ) {
        const qint64 x = index % imageWidth;
        const qint64 y = index / imageWidth;
        const qint64 cx = x / cellSize;
        const qint64 cy = y / cellSize;
        for ( qint64 dy = -1; dy <= 1; ++dy )
        {
            for ( qint64 dx = -1; dx <= 1; ++dx )
            {
                const auto it = cells.constFind( ( cy + dy ) * 1000003 + ( cx + dx ) );
                if ( it == cells.constEnd() )
                    continue;
                for ( const qint64 candidate : it.value() )
                {
                    const double px = double( candidate % imageWidth );
                    const double py = double( candidate / imageWidth );
                    const double dist2 = ( px - x ) * ( px - x ) + ( py - y ) * ( py - y );
                    if ( dist2 <= tolerance2 )
                        return true;
                }
            }
        }
        return false;
    };

    const QHash<qint64, QVector<qint64>> predictedCells = buildCells( predictedBoundary );
    qint64 truthMatched = 0;
    for ( const qint64 index : truthBoundary )
    {
        if ( nearInCells( predictedCells, index ) )
            ++truthMatched;
    }
    // Precision needs the REVERSE direction: how many PREDICTED boundary
    // pixels lie near some truth pixel. Counting truth matches only would
    // reward spraying predictions.
    const QHash<qint64, QVector<qint64>> truthCells = buildCells( truthBoundary );
    qint64 predictedMatched = 0;
    for ( const qint64 index : predictedBoundary )
    {
        if ( nearInCells( truthCells, index ) )
            ++predictedMatched;
    }
    const double precision = safeDivide( double( predictedMatched ),
                                         double( predictedBoundary.size() ) );
    const double recall = safeDivide( double( truthMatched ), double( truthBoundary.size() ) );
    return ResultT::success( precision + recall > 0.0
                                 ? 2.0 * precision * recall / ( precision + recall )
                                 : 0.0 );
}

// --- Detection AP ------------------------------------------------------------------------

double DetectionBox::iouWith( const DetectionBox &other ) const
{
    const double intersectionWidth =
        qMin( x + width, other.x + other.width ) - qMax( x, other.x );
    const double intersectionHeight =
        qMin( y + height, other.y + other.height ) - qMax( y, other.y );
    if ( intersectionWidth <= 0.0 || intersectionHeight <= 0.0 )
        return 0.0;
    const double intersection = intersectionWidth * intersectionHeight;
    const double unionArea = width * height + other.width * other.height - intersection;
    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

double averagePrecision( const QVector<DetectionBox> &sortedDetections,
                         qint64 groundTruthCount )
{
    if ( groundTruthCount <= 0 || sortedDetections.isEmpty() )
        return 0.0;
    qint64 truePositives = 0;
    double precisionSum = 0.0;
    int rank = 0;
    int index = 0;
    const int total = sortedDetections.size();
    while ( index < total )
    {
        // Confidence ties share ONE operating point: the whole tie block is
        // evaluated at its LAST rank, so an arbitrary input order inside the
        // block cannot swing the score.
        const double confidence = sortedDetections.at( index ).confidence;
        const int blockStart = index;
        qint64 blockTruePositives = 0;
        while ( index < total && sortedDetections.at( index ).confidence == confidence )
        {
            if ( sortedDetections.at( index ).isTruePositive )
                ++blockTruePositives;
            ++index;
        }
        rank += index - blockStart;
        truePositives += blockTruePositives;
        if ( blockTruePositives > 0 )
        {
            const double blockPrecision =
                double( truePositives ) / double( rank );
            precisionSum += blockPrecision * double( blockTruePositives );
        }
    }
    return double( precisionSum ) / double( groundTruthCount );
}

double meanAveragePrecision( const QVector<QVector<DetectionBox>> &perClassDetections,
                             const QVector<qint64> &perClassTruthCounts )
{
    if ( perClassDetections.size() != perClassTruthCounts.size() ||
         perClassDetections.isEmpty() )
        return 0.0;
    double sum = 0.0;
    for ( int i = 0; i < perClassDetections.size(); ++i )
    {
        QVector<DetectionBox> sorted = perClassDetections.at( i );
        std::sort( sorted.begin(), sorted.end(),
                   []( const DetectionBox &a, const DetectionBox &b ) {
                       if ( a.confidence != b.confidence )
                           return a.confidence > b.confidence;
                       return a.x < b.x; // deterministic tie-break
                   } );
        sum += averagePrecision( sorted, perClassTruthCounts.at( i ) );
    }
    return sum / double( perClassDetections.size() );
}

// --- EvaluationProtocol ---------------------------------------------------------------------

Result<void> EvaluationProtocol::validate() const
{
    using ResultT = Result<void>;
    auto fail = []( const QString &message ) {
        return ResultT::failure(
            Diagnostic{ QStringLiteral( "evaluation.protocol_invalid" ), message,
                        DiagnosticSeverity::Error } );
    };
    if ( m_datasetVersionId.isEmpty() || m_splitManifestId.isEmpty() )
        return fail( QStringLiteral( "protocol requires dataset version + split manifest" ) );
    if ( m_subset.isEmpty() )
        return fail( QStringLiteral( "protocol requires a subset" ) );
    // qIsFinite first: every ordered comparison against NaN is false, so the
    // range checks alone would let NaN through — and toJson serializes NaN as
    // null, which fromJson reads back as the default. A round-trip must not
    // be able to rewrite the protocol silently.
    if ( !qIsFinite( m_iouThreshold ) || m_iouThreshold <= 0.0 || m_iouThreshold > 1.0 )
        return fail( QStringLiteral( "IoU threshold must be in (0,1]" ) );
    if ( !qIsFinite( m_confidenceThreshold ) || m_confidenceThreshold < 0.0 ||
         m_confidenceThreshold >= 1.0 )
        return fail( QStringLiteral( "confidence threshold must be in [0,1)" ) );
    if ( m_aggregation != QLatin1String( "macro" ) &&
         m_aggregation != QLatin1String( "micro" ) &&
         m_aggregation != QLatin1String( "weighted" ) )
        return fail( QStringLiteral( "aggregation must be macro|micro|weighted" ) );
    return ResultT::success();
}

QJsonObject EvaluationProtocol::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    json.insert( QStringLiteral( "subset" ), m_subset );
    if ( !m_ignoreLabels.isEmpty() )
        json.insert( QStringLiteral( "ignore_labels" ),
                        QJsonArray::fromStringList( m_ignoreLabels ) );
    if ( !m_maskRef.isEmpty() )
        json.insert( QStringLiteral( "mask_ref" ), m_maskRef );
    json.insert( QStringLiteral( "iou_threshold" ), m_iouThreshold );
    json.insert( QStringLiteral( "confidence_threshold" ), m_confidenceThreshold );
    json.insert( QStringLiteral( "aggregation" ), m_aggregation );
    return json;
}

Result<EvaluationProtocol> EvaluationProtocol::fromJson( const QJsonObject &json )
{
    using ResultT = Result<EvaluationProtocol>;
    EvaluationProtocol protocol;
    protocol.m_datasetVersionId =
        json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    protocol.m_splitManifestId =
        json.value( QStringLiteral( "split_manifest_id" ) ).toString();
    protocol.m_subset =
        json.value( QStringLiteral( "subset" ) ).toString( QStringLiteral( "test" ) );
    protocol.m_ignoreLabels =
        json.value( QStringLiteral( "ignore_labels" ) ).toVariant().toStringList();
    protocol.m_maskRef = json.value( QStringLiteral( "mask_ref" ) ).toString();
    protocol.m_iouThreshold = json.value( QStringLiteral( "iou_threshold" ) ).toDouble( 0.5 );
    protocol.m_confidenceThreshold =
        json.value( QStringLiteral( "confidence_threshold" ) ).toDouble( 0.0 );
    protocol.m_aggregation =
        json.value( QStringLiteral( "aggregation" ) ).toString( QStringLiteral( "macro" ) );
    const auto validated = protocol.validate();
    if ( !validated )
        return ResultT::failure( validated.diagnostics() );
    return ResultT::success( protocol );
}

QJsonObject MetricRecord::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "run_id" ), runId );
    json.insert( QStringLiteral( "protocol" ), protocol.toJson() );
    json.insert( QStringLiteral( "metrics" ), metrics );
    json.insert( QStringLiteral( "metrics_hash" ), metricsHash );
    // M4 metrics schema versioning: readers refuse foreign layouts instead
    // of silently reinterpreting documents (missing key = v1-before-versioning).
    json.insert( QStringLiteral( "metrics_schema_version" ), metricsSchemaVersion );
    return json;
}

Result<MetricRecord> MetricRecord::fromJson( const QJsonObject &json )
{
    using ResultT = Result<MetricRecord>;
    MetricRecord record;
    record.runId = json.value( QStringLiteral( "run_id" ) ).toString();
    const auto protocol =
        EvaluationProtocol::fromJson( json.value( QStringLiteral( "protocol" ) ).toObject() );
    if ( !protocol )
        return ResultT::failure( protocol.diagnostics() );
    record.protocol = protocol.value();
    record.metrics = json.value( QStringLiteral( "metrics" ) ).toObject();
    record.metricsHash = json.value( QStringLiteral( "metrics_hash" ) ).toString();
    record.metricsSchemaVersion =
        json.value( QStringLiteral( "metrics_schema_version" ) ).toInteger( 1 );
    // Layout contract (evaluation.h): readers refuse foreign versions rather
    // than silently reinterpreting documents. A missing key reads as v1 —
    // records written before the field existed stay readable.
    if ( record.metricsSchemaVersion != kMetricsSchemaVersion )
    {
        return ResultT::failure( Diagnostic{
            QStringLiteral( "evaluation.version" ),
            QStringLiteral( "metrics layout version %1 not supported (expected %2)" )
                .arg( record.metricsSchemaVersion )
                .arg( kMetricsSchemaVersion ),
            DiagnosticSeverity::Error } );
    }
    if ( record.runId.isEmpty() )
    {
        return ResultT::failure( Diagnostic{ QStringLiteral( "evaluation.invalid" ),
                                             QStringLiteral( "metric record needs run id" ),
                                             DiagnosticSeverity::Error } );
    }
    return ResultT::success( record );
}

QString MetricRecord::contentHash() const
{
    // Binds the record CONTENT (protocol, metric document, layout version)
    // over the same canonical-JSON recipe the execution and result
    // fingerprints use (data/execution_fingerprint.h). The run id is
    // deliberately excluded: it is the record's row identity (re-keyed
    // legally by the offline bundle importer), while this hash certifies
    // what the record says. Key insertion order and formatting must not
    // move the hash; distinct doubles must.
    QJsonObject body;
    body.insert( QStringLiteral( "protocol" ), protocol.toJson() );
    body.insert( QStringLiteral( "metrics" ), metrics );
    body.insert( QStringLiteral( "metrics_schema_version" ), metricsSchemaVersion );
    return QString::fromLatin1(
        QCryptographicHash::hash( sicnu::data::canonicalizeJsonRfc8785( body ),
                                  QCryptographicHash::Sha256 )
            .toHex() );
}

} // namespace sicnu::experiment
