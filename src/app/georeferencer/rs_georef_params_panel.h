#ifndef RS_GEOREF_PARAMS_PANEL_H
#define RS_GEOREF_PARAMS_PANEL_H

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcptransformer.h"
#include "qgsimagewarper.h"

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QgsProjectionSelectionWidget;
class RsRmsScatterWidget;

/**
 * Right-side parameter dock content for the Georeferencer main window.
 *
 * Five sections:
 *   1. 坐标变换 (Transform) — 7-method combo + min/actual/DOF labels
 *   2. 重采样 (Resampling) — algorithm combo, output pixel size, output
 *      extent (read-only), background value
 *   3. RMS 误差分布 — embedded RsRmsScatterWidget + X/Y/Total/Max labels
 *   4. 坐标系 (CRS) — read-only labels (src/dst/projection name)
 *   5. 输出 (Output) — file path edit + Browse… button
 *
 * RPC mode (Task 8) is stubbed via setRpcMode(); the DEM section is not
 * built in Task 7 — demPath() returns empty and isDemSectionVisible()
 * returns false.
 */
class RsGeorefParamsPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit RsGeorefParamsPanel( QWidget *parent = nullptr );

    QgsGcpTransformerInterface::TransformMethod transformMethod() const;
    QgsImageWarper::ResamplingMethod resamplingMethod() const;
    QString outputPath() const;
    QgsCoordinateReferenceSystem destCrs() const;
    void setDestCrs( const QgsCoordinateReferenceSystem &crs );
    double outputPixelSize() const;

    /// Task 11.6.2 — workflow restore helpers.
    void setTransformMethod( QgsGcpTransformerInterface::TransformMethod m );
    void setResamplingMethod( QgsImageWarper::ResamplingMethod m );
    void setOutputPath( const QString &path );
    void setDemPath( const QString &path );

    // Stubs for Task 8
    bool isDemSectionVisible() const;
    QString demPath() const;
    void setRpcMode( bool on );

    /// Task 11.5.4 — DEM Z-offset (metres) from the params panel spin box.
    double demZOffset() const;

    /**
     * Task 11.5.5 — display the before/after RMS comparison produced by the
     * RPC linear-bias GCP refinement step.  Values are in destination-CRS
     * units (typically degrees for the RPC pipeline) and rendered with 3
     * fractional digits.  When \a after is strictly less than \a before the
     * "after" label is coloured green; otherwise neutral gray.
     */
    void setRefinementRms( double before, double after );

    /**
     * Clear refinement RMS labels to the empty placeholder (—).
     * Used for non-RPC methods and when fewer than 3 enabled GCPs are available.
     */
    void clearRefinementRms();

  public slots:
    /// Update the section-3 labels and DOF readout.
    void setRmsValues( int total, int enabled, double rmsPx,
                       double xRms, double yRms, double maxRms, int maxRmsRowId );
    /// Push residual points (dx,dy in px) to the embedded scatter widget.
    void setResidualScatter( const QVector<QPointF> &dxdy );
    void setMinimumGcpCount( int n );
    void setActualGcpCount( int n );

  signals:
    void transformMethodChanged();
    void resamplingMethodChanged();
    void outputPathChanged( const QString &path );
    /// Task 11.5.1 — emitted when the destination CRS picker changes.
    void destCrsChanged();
    /// Task 11.5.4 — emitted when the DEM Z-offset spin box value changes.
    void demZOffsetChanged();

  private slots:
    void onBrowseOutput();

  private:
    QComboBox *mTransformCombo = nullptr;
    QLabel *mMinPtsLabel = nullptr;
    QLabel *mActualPtsLabel = nullptr;
    QLabel *mDofLabel = nullptr;

    QComboBox *mResamplingCombo = nullptr;
    QDoubleSpinBox *mPixelSize = nullptr;
    QLineEdit *mOutputExtent = nullptr;
    QSpinBox *mBackground = nullptr;

    RsRmsScatterWidget *mScatter = nullptr;
    QLabel *mXRms = nullptr;
    QLabel *mYRms = nullptr;
    QLabel *mTotalRms = nullptr;
    QLabel *mMaxRms = nullptr;
    // Task 11.5.5 — before/after RMS readout for RPC linear-bias refinement.
    QLabel *mRmsBefore = nullptr;
    QLabel *mRmsAfter = nullptr;

    QLabel *mSrcCrsLabel = nullptr;
    QgsProjectionSelectionWidget *mCrsWidget = nullptr;
    QLabel *mProjNameLabel = nullptr;

    QLineEdit *mOutputPath = nullptr;
    QPushButton *mBrowseBtn = nullptr;

    // Task 11.4.8 — RPC mode DEM section
    QFrame *mDemSection = nullptr;
    QLineEdit *mDemPath = nullptr;
    QPushButton *mDemBrowseBtn = nullptr;
    QDoubleSpinBox *mDemZOffset = nullptr;
};

#endif // RS_GEOREF_PARAMS_PANEL_H
