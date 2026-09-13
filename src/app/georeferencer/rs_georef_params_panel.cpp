#include "rs_georef_params_panel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "dialogs/dialog_help_catalog.h"
#include "qgsprojectionselectionwidget.h"
#include "rs_rms_scatter_widget.h"

namespace
{
  void setHelp( QWidget *w, const QString &tip )
  {
    if ( !w )
      return;
    w->setToolTip( tip );
    w->setWhatsThis( tip );
    w->setStatusTip( tip );
  }

  QLabel *formLabel( const QString &text, const QString &tip, QWidget *parent )
  {
    auto *lbl = new QLabel( text, parent );
    setHelp( lbl, tip );
    return lbl;
  }

  QFrame *makeSectionFrame( const QString &title, QWidget *parent, const QString &sectionTip = {} )
  {
    auto *frame = new QFrame( parent );
    frame->setObjectName( QStringLiteral( "rsParamSection" ) );
    frame->setFrameShape( QFrame::StyledPanel );
    auto *lay = new QVBoxLayout( frame );
    lay->setContentsMargins( 8, 6, 8, 8 );
    lay->setSpacing( 6 );
    auto *header = new QLabel( title, frame );
    header->setObjectName( QStringLiteral( "rsParamSectionTitle" ) );
    QFont f = header->font();
    f.setBold( true );
    header->setFont( f );
    if ( !sectionTip.isEmpty() )
    {
      header->setToolTip( sectionTip );
      header->setWhatsThis( sectionTip );
      header->setStatusTip( sectionTip );
      frame->setToolTip( sectionTip );
      frame->setWhatsThis( sectionTip );
    }
    lay->addWidget( header );
    return frame;
  }
}

RsGeorefParamsPanel::RsGeorefParamsPanel( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsGeorefParamsPanel" ) );
  setMinimumWidth( 320 );
  setHelp( this, SicnuDialogHelp::shortForTool(
             QStringLiteral( "georef_params" ),
             tr( "Correction Parameters Panel" ) ) );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 6, 6, 6, 6 );
  root->setSpacing( 8 );

  // ---- Help banner ----
  {
    auto *banner = new QFrame( this );
    banner->setObjectName( QStringLiteral( "rsGeorefParamsHelpBanner" ) );
    banner->setFrameShape( QFrame::StyledPanel );
    banner->setStyleSheet(
      QStringLiteral( "QFrame#rsGeorefParamsHelpBanner {"
                      "  background:#f6f8fa; border:1px solid #d0d7de; border-radius:4px; }" ) );
    auto *bl = new QHBoxLayout( banner );
    bl->setContentsMargins( 8, 6, 8, 6 );
    auto *sum = new QLabel(
      SicnuDialogHelp::shortForTool( QStringLiteral( "georef_params" ),
                                     tr( "Correction parameters: transform / resampling / residuals / CRS / output" ) ),
      banner );
    sum->setWordWrap( true );
    setHelp( sum, tr(
      "This panel controls all write-out parameters of the geometric correction.\n"
      "Hover the section titles and widgets for details; press 'Parameter Description' for the full documentation.")  );
    auto *helpBtn = new QPushButton( tr( "Parameter Description" ), banner );
    helpBtn->setObjectName( QStringLiteral( "rsGeorefParamsHelpBtn" ) );
    helpBtn->setFlat( false );
    setHelp( helpBtn, tr( "Opens the full 'Correction Parameters' explanation (transform method, point counts, resampling, RMS, CRS, output)." ) );
    connect( helpBtn, &QPushButton::clicked, this, [this]() {
      SicnuDialogHelp::showToolHelp( this, QStringLiteral( "georef_params" ),
                                     tr( "Correction Parameters Panel" ) );
    } );
    bl->addWidget( sum, 1 );
    bl->addWidget( helpBtn, 0, Qt::AlignTop );
    root->addWidget( banner );
  }

  // ---- Section 1: 坐标变换 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "Coordinate Transformation" ), this,
      tr(
        "[Transform] Fits a geometric model from source image coordinates to target coordinates using GCPs.\n"
        "Each method needs a different minimum point count; fitting is unreliable below it and 'Run' is disabled.\n"
        "Same-scene registration usually needs only Linear or a first-order polynomial; use higher orders / TPS for complex distortions." ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mTransformCombo = new QComboBox( sec );
    mTransformCombo->setObjectName( QStringLiteral( "rsTransformCombo" ) );
    using TM = QgsGcpTransformerInterface::TransformMethod;
    const QVector<QPair<TM, QString>> methods = {
      { TM::Linear, tr( "Linear" ) },
      { TM::Helmert, tr( "Helmert" ) },
      { TM::PolynomialOrder1, tr( "Polynomial Order 1" ) },
      { TM::PolynomialOrder2, tr( "Polynomial Order 2" ) },
      { TM::PolynomialOrder3, tr( "Polynomial Order 3" ) },
      { TM::ThinPlateSpline, tr( "Thin Plate Spline" ) },
      { TM::Projective, tr( "Projective" ) },
      { TM::RpcPhysical, tr( "RPC Physical (RFM)" ) },
    };
    for ( const auto &m : methods )
      mTransformCombo->addItem( m.second, QVariant::fromValue( static_cast<int>( m.first ) ) );

    // Ensure the combo view is a QListView so we can hide individual rows
    // when toggling RPC mode (see setRpcMode()).
    mTransformCombo->setView( new QListView( mTransformCombo ) );

    connect( mTransformCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { emit transformMethodChanged(); } );

    setHelp( mTransformCombo, tr(
      "Transform method (geometric model):\n"
      "• Linear (≥ 2 points): translation + scale; common for the same scene / nearly collinear cases\n"
      "• Helmert (≥ 2 points): similarity transform (rotation + uniform scale)\n"
      "• Polynomial 1 (≥ 3 points): affine; corrects rotation / shear\n"
      "• Polynomial 2 / 3 (≥ 6/10 points): bending deformation; high orders overfit easily\n"
      "• TPS thin plate spline: strong local deformation; GCPs should be evenly spread\n"
      "• Projective: perspective (scanned maps, oblique imagery)\n"
      "• RPC Physical: sensor RPC, Image→Map only; requires metadata and an optional DEM\n\n"
      "Tip: with exactly the minimum points, DOF = 0 and residuals approach 0 — that does not mean good accuracy; collect more points.")  );
    form->addRow( formLabel( tr( "Method" ),
                              tr( "Geometric transformation model. Hover the combo box for per-method descriptions and minimum point counts." ), sec ),
                 mTransformCombo );

    mMinPtsLabel = new QLabel( tr( "—" ), sec );
    mActualPtsLabel = new QLabel( tr( "—" ), sec );
    mDofLabel = new QLabel( tr( "—" ), sec );
    mMinPtsLabel->setObjectName( QStringLiteral( "rsMinPtsLabel" ) );
    mActualPtsLabel->setObjectName( QStringLiteral( "rsActualPtsLabel" ) );
    mDofLabel->setObjectName( QStringLiteral( "rsDofLabel" ) );
    setHelp( mMinPtsLabel, tr(
      "Minimum points: the lower bound of 'enabled' GCPs required by the current transform method.\n"
      "For example, a cubic polynomial usually needs about 10 points. Below the minimum, fitting is unreliable.")  );
    setHelp( mActualPtsLabel, tr(
      "Usable points: the number of control points ticked 'enabled' in the GCP table.\n"
      "Only enabled points take part in the fit and RMS computation.")  );
    setHelp( mDofLabel, tr(
      "Degrees of freedom DOF = usable points − minimum points.\n"
      "• DOF < 0: not enough points to fit\n"
      "• DOF = 0: exactly determined; residuals are 'fitted away' to almost always 0, with no statistical meaning\n"
      "• DOF > 0: over-determined; assess accuracy via RMS — collecting more evenly distributed points is advisable")  );

    form->addRow( formLabel( tr( "Minimum Points" ),
                              tr( "Minimum number of enabled GCPs required by the method." ), sec ), mMinPtsLabel );
    form->addRow( formLabel( tr( "Actually usable points" ),
                              tr( "Number of GCPs enabled and used in the fit." ), sec ), mActualPtsLabel );
    form->addRow( formLabel( tr( "Degrees of Freedom" ),
                              tr( "Actual points minus the minimum required. Residual analysis needs > 0." ), sec ), mDofLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 2: 重采样 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "Resampling" ), this,
      tr(
        "[Resampling] Pixel interpolation used when warping the source image onto the target grid by the transform model.\n"
        "Affects only the output's smoothness / sharpness; the GCP geometric fit itself is unchanged.") ) ;
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mResamplingCombo = new QComboBox( sec );
    mResamplingCombo->setObjectName( QStringLiteral( "rsResamplingCombo" ) );
    using RM = QgsImageWarper::ResamplingMethod;
    mResamplingCombo->addItem( tr( "Nearest Neighbour" ), QVariant::fromValue( static_cast<int>( RM::NearestNeighbour ) ) );
    mResamplingCombo->addItem( tr( "Bilinear" ), QVariant::fromValue( static_cast<int>( RM::Bilinear ) ) );
    mResamplingCombo->addItem( tr( "Cubic" ), QVariant::fromValue( static_cast<int>( RM::Cubic ) ) );
    mResamplingCombo->addItem( tr( "Cubic Spline" ), QVariant::fromValue( static_cast<int>( RM::CubicSpline ) ) );
    mResamplingCombo->addItem( tr( "Lanczos" ), QVariant::fromValue( static_cast<int>( RM::Lanczos ) ) );
    connect( mResamplingCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { emit resamplingMethodChanged(); } );
    setHelp( mResamplingCombo, tr(
      "Resampling algorithm (when writing the raster):\n"
      "• Nearest Neighbour: no neighbourhood mixing; first choice for classification / integer labels\n"
      "• Bilinear: balanced speed and quality; common for continuous grayscale / multispectral\n"
      "• Cubic: cubic convolution, smoother with slightly softer edges\n"
      "• Cubic Spline / Lanczos: higher order, sharper / slower; use with care for quantitative work\n\n"
      "Visual optics: Bilinear or Cubic; classification maps: Nearest.")  );
    form->addRow( formLabel( tr( "Algorithm" ), tr( "Pixel interpolation method." ), sec ), mResamplingCombo );

    mPixelSize = new QDoubleSpinBox( sec );
    mPixelSize->setObjectName( QStringLiteral( "rsPixelSize" ) );
    mPixelSize->setRange( 0.0, 1.0e9 );
    mPixelSize->setDecimals( 6 );
    mPixelSize->setValue( 0.0 );
    mPixelSize->setSpecialValueText( tr( "auto" ) );
    setHelp( mPixelSize, tr(
      "Output pixel size (ground units of the target CRS, e.g. metres).\n"
      "• auto (0): estimated by the engine from the input / reference\n"
      "• Manual: e.g. 30 means 30 m resolution (under UTM)\n"
      "For I2I alignment to the reference, the reference resolution or auto is typical.")  );
    form->addRow( formLabel( tr( "Output Pixel Size" ),
                              tr( "Target grid resolution; auto = automatic." ), sec ), mPixelSize );

    mOutputExtent = new QLineEdit( sec );
    mOutputExtent->setObjectName( QStringLiteral( "rsOutputExtent" ) );
    mOutputExtent->setReadOnly( true );
    mOutputExtent->setText( tr( "auto · ref" ) );
    setHelp( mOutputExtent, tr(
      "Output geographic extent (read-only preview).\n"
      "auto · ref: the extent follows the reference / transform result automatically; usually no change needed.")  );
    form->addRow( formLabel( tr( "Output Extent" ), tr( "Map extent covered by the result." ), sec ), mOutputExtent );

    mBackground = new QSpinBox( sec );
    mBackground->setObjectName( QStringLiteral( "rsBackground" ) );
    mBackground->setRange( 0, 65535 );
    mBackground->setValue( 0 );
    setHelp( mBackground, tr(
      "Background / fill value: written to pixels the warp leaves without source data.\n"
      "Usually 0; if 0 is a valid DN, use e.g. 65535 instead and set it as NoData in the result.")  );
    connect( mBackground, QOverload<int>::of( &QSpinBox::valueChanged ),
             this, [this]( int v ) { emit backgroundValueChanged( v ); } );
    form->addRow( formLabel( tr( "Background Value" ), tr( "Pixel value used to fill holes." ), sec ), mBackground );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 3: RMS 误差分布 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "RMS Error Distribution" ), this,
      tr(
        "[RMS Error] Root mean square of 'predicted − observed' positions over enabled GCPs.\n"
        "Usually in source image pixels (px). With accurate conjugate points and a suitable model, the RMS should be small.\n"
        "With DOF = 0, residuals are fitted to nearly 0 and do not represent real accuracy — collect more points.") ) ;
    auto *vbox = new QVBoxLayout();
    vbox->setContentsMargins( 0, 0, 0, 0 );

    mScatter = new RsRmsScatterWidget( sec );
    setHelp( mScatter, tr(
      "Residual scatter plot:\n"
      "• Horizontal axis ≈ ΔX (column-direction residual)\n"
      "• Vertical axis ≈ ΔY (row-direction residual)\n"
      "Points should stay near the origin and be roughly isotropic. For outliers: check whether the wrong feature was picked, or disable the point in the GCP table.")  );
    vbox->addWidget( mScatter, 0, Qt::AlignHCenter );

    auto *grid = new QFormLayout();
    grid->setContentsMargins( 0, 0, 0, 0 );
    mXRms = new QLabel( tr( "—" ), sec );
    mYRms = new QLabel( tr( "—" ), sec );
    mTotalRms = new QLabel( tr( "—" ), sec );
    mMaxRms = new QLabel( tr( "—" ), sec );
    mXRms->setObjectName( QStringLiteral( "rsXRmsLabel" ) );
    mYRms->setObjectName( QStringLiteral( "rsYRmsLabel" ) );
    mTotalRms->setObjectName( QStringLiteral( "rsTotalRmsLabel" ) );
    mMaxRms->setObjectName( QStringLiteral( "rsMaxRmsLabel" ) );
    setHelp( mXRms, tr( "Root-mean-square of X residuals (pixels)." ) );
    setHelp( mYRms, tr( "Root-mean-square of Y residuals (pixels)." ) );
    setHelp( mTotalRms, tr(
      "Total RMS: root mean square of the residual magnitudes of all enabled GCPs.\n"
      "Visual same-scene registration: a few pixels is the norm; hundreds to thousands means checking CRS / Sync zoom / point picking.")  );
    setHelp( mMaxRms, tr(
      "The maximum residual and its GCP number. Check first whether that point was picked wrongly or suffers edge distortion.")  );
    grid->addRow( formLabel( tr( "X RMS" ), tr( "RMS of X residuals." ), sec ), mXRms );
    grid->addRow( formLabel( tr( "Y RMS" ), tr( "RMS of Y residuals." ), sec ), mYRms );
    grid->addRow( formLabel( tr( "Total RMS" ), tr( "Total residual RMS." ), sec ), mTotalRms );
    grid->addRow( formLabel( tr( "Maximum Residual" ), tr( "The worst single GCP." ), sec ), mMaxRms );

    // Task 11.5.5 — before/after RMS readout for RPC linear-bias refinement.
    // Empty until the main window calls setRefinementRms().
    mRmsBefore = new QLabel( QString(), sec );
    mRmsBefore->setObjectName( QStringLiteral( "rsRmsBefore" ) );
    mRmsAfter = new QLabel( QString(), sec );
    mRmsAfter->setObjectName( QStringLiteral( "rsRmsAfter" ) );
    setHelp( mRmsBefore, tr( "RMS before RPC refinement (shown with ≥ 3 GCPs)." ) );
    setHelp( mRmsAfter, tr( "RMS after RPC linear-bias refinement; green means improvement." ) );

    vbox->addItem( grid );
    vbox->addWidget( mRmsBefore );
    vbox->addWidget( mRmsAfter );
    sec->layout()->addItem( vbox );
    root->addWidget( sec );
  }

  // ---- Section 4: 坐标系 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "Coordinate System" ), this,
      tr(
        "[CRS] The target CRS determines the output GeoTIFF projection and how GCP target coordinates are interpreted.\n"
        "I2I: usually identical to the reference image CRS (aligned automatically when the reference loads).\n"
        "I2M: the Map canvas follows the target CRS when showing main project layers.") ) ;
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mSrcCrsLabel = new QLabel( tr( "—" ), sec );
    mSrcCrsLabel->setObjectName( QStringLiteral( "rsSrcCrsLabel" ) );
    setHelp( mSrcCrsLabel, tr(
      "The source image (Warp) CRS. Shows — when undefined.\n"
      "Picked coordinates follow the layer CRS.")  );

    // Task 11.5.1 — real CRS picker replaces the hard-coded EPSG:32650 label.
    mCrsWidget = new QgsProjectionSelectionWidget( sec );
    mCrsWidget->setObjectName( QStringLiteral( "rsCrsWidget" ) );
    setHelp( mCrsWidget, tr(
      "Target CRS: the coordinate system of the correction result and of the fit.\n"
      "Common choices: WGS 84 / UTM zone xxN, CGCS2000 Gauss projection, etc.\n"
      "In I2I, it is set to the reference CRS automatically once the reference image loads.")  );

    // Restore last user choice (default to EPSG:32650 to preserve previous
    // behaviour from Task 11.4 when no setting exists yet).
    {
      const QString lastAuthid = QSettings()
                                   .value( QStringLiteral( "Georeferencer/lastDestCrs" ),
                                           QStringLiteral( "EPSG:32650" ) )
                                   .toString();
      QgsCoordinateReferenceSystem saved;
      if ( !lastAuthid.isEmpty() )
        saved.createFromOgcWmsCrs( lastAuthid );
      if ( saved.isValid() )
        mCrsWidget->setCrs( saved );
    }

    connect( mCrsWidget, &QgsProjectionSelectionWidget::crsChanged, this,
             [this]( const QgsCoordinateReferenceSystem &crs ) {
               QSettings().setValue( QStringLiteral( "Georeferencer/lastDestCrs" ),
                                     crs.authid() );
               if ( mProjNameLabel )
                 mProjNameLabel->setText( crs.description().isEmpty() ? crs.authid()
                                                                     : crs.description() );
               emit destCrsChanged();
             } );

    mProjNameLabel = new QLabel( sec );
    mProjNameLabel->setObjectName( QStringLiteral( "rsProjNameLabel" ) );
    {
      const QgsCoordinateReferenceSystem cur = mCrsWidget->crs();
      mProjNameLabel->setText( cur.description().isEmpty() ? cur.authid() : cur.description() );
    }

    form->addRow( formLabel( tr( "Source CRS" ), tr( "Source image CRS." ), sec ), mSrcCrsLabel );
    form->addRow( formLabel( tr( "Target CRS" ), tr( "Result and fit target coordinate system." ), sec ), mCrsWidget );
    form->addRow( formLabel( tr( "Projection Name" ), tr( "Human-readable name of the target CRS." ), sec ), mProjNameLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 5: 输出 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "Outputs" ), this,
      tr(
        "[Output] Save path of the corrected GeoTIFF.\n"
        "The toolbar 'Run' enables only after a valid path is entered (and GCP counts / fitting conditions are met).\n"
        "The task list records this path so the result can be loaded into the main project when finished.") ) ;
    auto *row = new QHBoxLayout();
    row->setContentsMargins( 0, 0, 0, 0 );

    mOutputPath = new QLineEdit( sec );
    mOutputPath->setObjectName( QStringLiteral( "rsOutputPath" ) );
    mOutputPath->setPlaceholderText( tr( "/path/to/output.tif" ) );
    connect( mOutputPath, &QLineEdit::textChanged,
             this, [this]( const QString &s ) { emit outputPathChanged( s ); } );
    setHelp( mOutputPath, tr(
      "Full path of the output file; .tif / .tiff recommended.\n"
      "The directory must be writable; same-named files may be overwritten (depending on the task implementation).")  );

    mBrowseBtn = new QPushButton( tr( "Browse…" ), sec );
    mBrowseBtn->setObjectName( QStringLiteral( "rsBrowseOutputBtn" ) );
    connect( mBrowseBtn, &QPushButton::clicked, this, &RsGeorefParamsPanel::onBrowseOutput );
    setHelp( mBrowseBtn, tr( "Browse and choose the output file location." ) );

    row->addWidget( mOutputPath, 1 );
    row->addWidget( mBrowseBtn );
    sec->layout()->addItem( row );
    root->addWidget( sec );
  }

  // ---- Section 6: DEM (RPC mode only) ----
  {
    mDemSection = makeSectionFrame(
      tr( "DEM (RPC mode)" ), this,
      tr(
        "[DEM] Shown only when the transform method is RPC Physical.\n"
        "An optional DEM improves RPC projection heights; the Z offset is a metric correction relative to the DEM.")  );
    mDemSection->setObjectName( QStringLiteral( "rsDemSection" ) );

    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    auto *demRow = new QHBoxLayout();
    demRow->setContentsMargins( 0, 0, 0, 0 );
    mDemPath = new QLineEdit( mDemSection );
    mDemPath->setObjectName( QStringLiteral( "rsDemPath" ) );
    mDemPath->setPlaceholderText( tr( "/path/to/dem.tif (optional)" ) );
    setHelp( mDemPath, tr( "DEM path (optional), used for RPC height-related projection." ) );
    mDemBrowseBtn = new QPushButton( tr( "Browse…" ), mDemSection );
    mDemBrowseBtn->setObjectName( QStringLiteral( "rsDemBrowseBtn" ) );
    connect( mDemBrowseBtn, &QPushButton::clicked, this, [this]() {
      const QString path = QFileDialog::getOpenFileName(
        this,
        tr( "Select DEM File" ),
        mDemPath->text(),
        tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
      if ( !path.isEmpty() )
        setDemPath( path );
    } );
    setHelp( mDemBrowseBtn, tr( "Selects the DEM raster file." ) );
    demRow->addWidget( mDemPath, 1 );
    demRow->addWidget( mDemBrowseBtn );
    form->addRow( tr( "DEM path" ), demRow );

    mDemZOffset = new QDoubleSpinBox( mDemSection );
    mDemZOffset->setObjectName( QStringLiteral( "rsDemZOffset" ) );
    mDemZOffset->setRange( -10000.0, 10000.0 );
    mDemZOffset->setDecimals( 2 );
    mDemZOffset->setValue( 0.0 );
    mDemZOffset->setSuffix( tr( " m" ) );
    // Task 11.5.4 — propagate spin-box edits so the main window can
    // recompute the RPC fit (and the warp pipeline picks up RPC_HEIGHT).
    connect( mDemZOffset, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, [this]( double ) { emit demZOffsetChanged(); } );
    setHelp( mDemZOffset, tr( "Elevation offset relative to the DEM (m), passed as the RPC_HEIGHT option." ) );
    form->addRow( tr( "Elevation Offset" ), mDemZOffset );

    mDemSection->layout()->addItem( form );
    mDemSection->setVisible( false );
    root->addWidget( mDemSection );
  }

  // Default profile is ImageToImage — hide RPC until I2M setProfile().
  applyProfileToMethodCombo();

  root->addStretch( 1 );
}

QgsGcpTransformerInterface::TransformMethod RsGeorefParamsPanel::transformMethod() const
{
  const int v = mTransformCombo->currentData().toInt();
  return static_cast<QgsGcpTransformerInterface::TransformMethod>( v );
}

QgsImageWarper::ResamplingMethod RsGeorefParamsPanel::resamplingMethod() const
{
  const int v = mResamplingCombo->currentData().toInt();
  return static_cast<QgsImageWarper::ResamplingMethod>( v );
}

QString RsGeorefParamsPanel::outputPath() const
{
  return mOutputPath->text().trimmed();
}

void RsGeorefParamsPanel::setTransformMethod( QgsGcpTransformerInterface::TransformMethod m )
{
  if ( !mTransformCombo )
    return;
  const int idx = mTransformCombo->findData( QVariant::fromValue( static_cast<int>( m ) ) );
  if ( idx >= 0 )
    mTransformCombo->setCurrentIndex( idx );
}

void RsGeorefParamsPanel::setResamplingMethod( QgsImageWarper::ResamplingMethod m )
{
  if ( !mResamplingCombo )
    return;
  const int idx = mResamplingCombo->findData( QVariant::fromValue( static_cast<int>( m ) ) );
  if ( idx >= 0 )
    mResamplingCombo->setCurrentIndex( idx );
}

void RsGeorefParamsPanel::setOutputPath( const QString &path )
{
  if ( mOutputPath )
    mOutputPath->setText( path );
}

void RsGeorefParamsPanel::setDemPath( const QString &path )
{
  if ( mDemPath )
    mDemPath->setText( path );
}

QgsCoordinateReferenceSystem RsGeorefParamsPanel::destCrs() const
{
  // Task 11.5.1 — picker is the source of truth. Fall back to EPSG:32650 if
  // the widget was never constructed (defensive — shouldn't happen).
  if ( mCrsWidget )
    return mCrsWidget->crs();
  return QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32650" ) );
}

void RsGeorefParamsPanel::setDestCrs( const QgsCoordinateReferenceSystem &crs )
{
  if ( !mCrsWidget )
    return;
  // QgsProjectionSelectionWidget emits crsChanged() when its current CRS
  // actually changes; our slot persists + emits destCrsChanged(). If the
  // incoming CRS equals the current one no signal would fire, so we fan it
  // out unconditionally here to honour the "setter triggers signal" contract
  // the test (and main-window recomputeFit wiring) depends on.
  const bool sameCrs = ( mCrsWidget->crs() == crs );
  mCrsWidget->setCrs( crs );
  if ( sameCrs )
  {
    QSettings().setValue( QStringLiteral( "Georeferencer/lastDestCrs" ), crs.authid() );
    if ( mProjNameLabel )
      mProjNameLabel->setText( crs.description().isEmpty() ? crs.authid() : crs.description() );
    emit destCrsChanged();
  }
}

double RsGeorefParamsPanel::outputPixelSize() const
{
  return mPixelSize->value();
}

bool RsGeorefParamsPanel::isDemSectionVisible() const
{
  // Use !isHidden() rather than isVisible(): the latter is false when the
  // window has not yet been shown, but the DEM-section's own visibility
  // intent is independently observable.  Tests construct the window without
  // showing it, so we report the local (unrealized) visibility state.
  return mDemSection && !mDemSection->isHidden();
}

QString RsGeorefParamsPanel::demPath() const
{
  return mDemPath ? mDemPath->text().trimmed() : QString();
}

double RsGeorefParamsPanel::demZOffset() const
{
  return mDemZOffset ? mDemZOffset->value() : 0.0;
}

void RsGeorefParamsPanel::setDemZOffset( double z )
{
  if ( mDemZOffset )
    mDemZOffset->setValue( z );
}

int RsGeorefParamsPanel::backgroundValue() const
{
  return mBackground ? mBackground->value() : 0;
}

void RsGeorefParamsPanel::setBackgroundValue( int v )
{
  if ( mBackground )
    mBackground->setValue( v );
}

void RsGeorefParamsPanel::setProfile( Profile p )
{
  mProfile = p;
  applyProfileToMethodCombo();
  if ( mProfile == Profile::ImageToImage )
  {
    setRpcMode( false );
  }
  else
  {
    setRpcMode( transformMethod() ==
                QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  }
}

void RsGeorefParamsPanel::applyProfileToMethodCombo()
{
  if ( !mTransformCombo )
    return;

  auto *view = qobject_cast<QListView *>( mTransformCombo->view() );
  const int rpcIdx = mTransformCombo->findData(
    QVariant::fromValue(
      static_cast<int>( QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) ) );

  // I2I: hide RPC row. I2M: show every method (including RPC).
  for ( int i = 0; i < mTransformCombo->count(); ++i )
  {
    const auto m = static_cast<QgsGcpTransformerInterface::TransformMethod>(
      mTransformCombo->itemData( i ).toInt() );
    const bool isRpc = ( m == QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
    const bool hide = ( mProfile == Profile::ImageToImage && isRpc );
    if ( view )
      view->setRowHidden( i, hide );
  }

  if ( mProfile == Profile::ImageToImage
       && transformMethod() == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
  {
    const int linIdx = mTransformCombo->findData(
      QVariant::fromValue(
        static_cast<int>( QgsGcpTransformerInterface::TransformMethod::Linear ) ) );
    if ( linIdx >= 0 )
      mTransformCombo->setCurrentIndex( linIdx );
  }

  Q_UNUSED( rpcIdx )
}

void RsGeorefParamsPanel::setRpcMode( bool on )
{
  // DEM section only — method combo is controlled by setProfile().
  if ( mDemSection )
    mDemSection->setVisible( on );
  // RPC warp (#363): GDAL RPC transformer outputs WGS84 degrees but the warp
  // path stamped the panel destCrs directly — force to EPSG:4326 and disable
  // picker while in RPC mode to avoid silent Earth-scale misplacement.
  if ( mCrsWidget )
  {
    mCrsWidget->setEnabled( !on );
    if ( on )
    {
      const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
      if ( mCrsWidget->crs() != wgs84 )
        setDestCrs( wgs84 );
      mCrsWidget->setToolTip( tr( "In RPC mode the target CRS is fixed to EPSG:4326 (WGS84 lat/lon, the RPC output space); reproject afterwards if a projection is needed" ) );
    }
    else
    {
      mCrsWidget->setToolTip( tr( "Target CRS: the coordinate system used for the correction result and the fit.\nCommon choices: WGS 84 / UTM zone xxN, CGCS2000 Gauss projection, etc.\nIn I2I, it is set to the reference CRS automatically once the reference image loads." ) );
    }
  }
}

void RsGeorefParamsPanel::setRmsValues( int /*total*/, int /*enabled*/,
                                        double rmsPx, double xRms, double yRms,
                                        double maxRms, int maxRmsRowId )
{
  auto fmt = []( double v ) { return QString::number( v, 'f', 3 ); };
  mXRms->setText( fmt( xRms ) + tr( " px" ) );
  mYRms->setText( fmt( yRms ) + tr( " px" ) );
  mTotalRms->setText( fmt( rmsPx ) + tr( " px" ) );
  if ( maxRmsRowId >= 0 )
    mMaxRms->setText( tr( "%1 px (row #%2)" ).arg( fmt( maxRms ) ).arg( maxRmsRowId + 1 ) );
  else
    mMaxRms->setText( fmt( maxRms ) + tr( " px" ) );
}

void RsGeorefParamsPanel::setResidualScatter( const QVector<QPointF> &dxdy )
{
  if ( mScatter )
    mScatter->setResiduals( dxdy );
}

void RsGeorefParamsPanel::setRefinementRms( double before, double after )
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "RMS before refinement: %1 px" ).arg( before, 0, 'f', 3 ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "RMS after refinement: %1 px" ).arg( after, 0, 'f', 3 ) );
    mRmsAfter->setStyleSheet( after < before
                                ? QStringLiteral( "color: #208830;" )
                                : QStringLiteral( "color: #5f6b7a;" ) );
  }
}

void RsGeorefParamsPanel::clearRefinementRms()
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "RMS before refinement: —" ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "RMS after refinement: —" ) );
    mRmsAfter->setStyleSheet( QString() );
  }
}

void RsGeorefParamsPanel::setMinimumGcpCount( int n )
{
  mMinPtsLabel->setText( QString::number( n ) );
}

void RsGeorefParamsPanel::setActualGcpCount( int n )
{
  mActualPtsLabel->setText( QString::number( n ) );

  // Update DOF: actual - min for current method. If actual < min, DOF = 0.
  bool ok = false;
  const int minN = mMinPtsLabel->text().toInt( &ok );
  if ( ok )
  {
    const int dof = std::max( 0, n - minN );
    mDofLabel->setText( QString::number( dof ) );
  }
}

void RsGeorefParamsPanel::onBrowseOutput()
{
  const QString path = QFileDialog::getSaveFileName(
    this,
    tr( "Select Output GeoTIFF" ),
    mOutputPath->text(),
    tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
  if ( path.isEmpty() )
    return;
  setOutputPath( path );
}
