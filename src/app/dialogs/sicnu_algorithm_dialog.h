// sicnu_algorithm_dialog.h — Concrete processing algorithm dialog that uses
// the full QGIS widget wrapper system (QgsProcessingGuiRegistry).
//
// Replaces the hand-rolled openProcessingAlgorithm() in main_window.cpp.
#pragma once

#include <gui/processing/qgsprocessingalgorithmdialogbase.h>
#include <gui/processing/qgsprocessingwidgetwrapper.h>

#include <QVector>
#include <QVariantMap>

class QCheckBox;
class QPlainTextEdit;
class QGroupBox;
class QgsProcessingFeedback;
class QgsProcessingParametersWidget;

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

    // Called when user clicks "Run"
    void runAlgorithm() override;

    // Called by the main window to set up the parameter UI
    void buildParameterWidgets();

  protected slots:
    void algExecuted( bool successful, const QVariantMap &results ) override;
    void updateCommandPreview();

  protected:
    void finished( bool successful, const QVariantMap &result,
                   QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;

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
};
