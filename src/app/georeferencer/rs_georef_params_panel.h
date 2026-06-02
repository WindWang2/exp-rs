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
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
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
    double outputPixelSize() const;

    // Stubs for Task 8
    bool isDemSectionVisible() const;
    QString demPath() const;
    void setRpcMode( bool on );

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

    QLabel *mSrcCrsLabel = nullptr;
    QLabel *mDstCrsLabel = nullptr;
    QLabel *mProjNameLabel = nullptr;

    QLineEdit *mOutputPath = nullptr;
    QPushButton *mBrowseBtn = nullptr;
};

#endif // RS_GEOREF_PARAMS_PANEL_H
