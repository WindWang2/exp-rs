// sicnu_algorithm_dialog.h — Concrete processing algorithm dialog that uses
// the full QGIS widget wrapper system (QgsProcessingGuiRegistry).
//
// Run path submits to JobEngine (processing: prefix / per-job callable) so
// toolbox algorithms appear in RsJobPanel with the rest of the unified queue.
#pragma once

#include <gui/processing/qgsprocessingalgorithmdialogbase.h>
#include <gui/processing/qgsprocessingwidgetwrapper.h>

#include <QString>
#include <QVector>
#include <QVariantMap>

#include <memory>

class QCheckBox;
class QPlainTextEdit;
class QGroupBox;
class QgsProcessingFeedback;
class QgsProcessingParametersWidget;
class QgsProcessingAlgorithm;

/**
 * Shared state for one JobEngine-backed processing run.
 * prepare() on GUI thread; runPrepared() on worker; postProcess() on GUI.
 */
struct SicnuProcessingRunState
{
  SicnuProcessingRunState();
  ~SicnuProcessingRunState(); // out-of-line: QgsProcessingAlgorithm incomplete here

  std::unique_ptr<QgsProcessingAlgorithm> algorithm;
  QVariantMap parameters;
  QVariantMap results;
  /** True once the JobEngine worker entered runPrepared (postProcess required). */
  bool workerStarted = false;
};

class SicnuAlgorithmDialog : public QgsProcessingAlgorithmDialogBase
{
    Q_OBJECT
  public:
    explicit SicnuAlgorithmDialog( QWidget *parent = nullptr );
    ~SicnuAlgorithmDialog() override;

    // QgsProcessingParametersGenerator interface
    QVariantMap createProcessingParameters( Flags flags = Flags() ) override;

    // QgsProcessingContextGenerator interface
    QgsProcessingContext *processingContext() override;

    // Called when user clicks "Run" — submits to JobEngine
    void runAlgorithm() override;

    // Called by the main window to set up the parameter UI
    void buildParameterWidgets();

  protected slots:
    void algExecuted( bool successful, const QVariantMap &results ) override;
    void updateCommandPreview();

  protected:
    void finished( bool successful, const QVariantMap &result,
                   QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;

    /** Stay alive while a JobEngine job is in flight (no mAlgorithmTask). */
    bool isFinalized() override;

  private slots:
    void onProcessingJobFinished( const QString &jobId );

  private:
    QgsProcessingContext mContext;
    QgsProcessingParametersWidget *mParamWidget = nullptr;
    QVector<QgsAbstractProcessingParameterWidgetWrapper *> mWrappers;
    qint64 mStartTime = 0;
    QVariantMap mHistoryDetails;
    long long mHistoryLogId = -1;
    QCheckBox *mLoadResultsCheck = nullptr;
    QgsProcessingFeedback *mFeedback = nullptr;
    QGroupBox *mCommandGroup = nullptr;
    QPlainTextEdit *mCommandPreview = nullptr;

    QString mPendingJobId;
    std::shared_ptr<SicnuProcessingRunState> mRunState;
    bool mJobBridgeConnected = false;
};
