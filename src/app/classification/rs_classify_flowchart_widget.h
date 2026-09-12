// rs_classify_flowchart_widget.h — Interactive flowchart panel for Classification workflow.
#pragma once

#include "rs_classify_workflow_controller.h"

#include <QWidget>
#include <QVector>

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QFrame;

/**
 * \brief Interactive flowchart panel displaying the 8-stage remote sensing classification pipeline.
 *
 * Shows the full end-to-end workflow:
 * 1. 打开源影像 (Source Raster)
 * 2. 定义分类体系 (Class System)
 * 3. 采集训练样本 (Sample Collection)
 * 4. 样本可分性评价 (Feature & Sample Evaluation)
 * 5. 分类器训练与全图分类 (Classifier & Classification)
 * 6. 分类精度评定 (Accuracy Assessment)
 * 7. 分类后处理 (Post Processing)
 * 8. 成果导出与加载 (Export & Loading)
 *
 * Provides real-time status tracking, metric badges, and interactive step navigation.
 */
class RsClassifyFlowchartWidget : public QWidget
{
    Q_OBJECT

public:
    enum class FlowStep {
        SourceRaster = 0,
        ClassSystem,
        SampleCollection,
        SampleEvaluation,
        TrainAndClassify,
        AccuracyAssessment,
        PostProcessing,
        ExportAndLoad,
        Count
    };

    explicit RsClassifyFlowchartWidget( QWidget *parent = nullptr );
    ~RsClassifyFlowchartWidget() override = default;

    /// Bind to workflow controller for real-time synchronization
    void bindController( RsClassifyWorkflowController *controller );

    /// Update dynamic metric summaries
    void setSourceRasterInfo( const QString &fileName, int width, int height, int bands, const QString &crs );
    void setClassCountInfo( int totalClasses );
    void setSampleInfo( int totalRois, int totalPixels );
    void setEvaluationInfo( const QString &summary );
    void setClassificationInfo( const QString &methodName, int durationMs );
    void setAccuracyInfo( double overallAccuracy, double kappa );
    void setPostProcessInfo( const QString &operations );
    void setExportInfo( const QString &outputPath );

    /// Current active step
    FlowStep activeStep() const { return m_activeStep; }
    void setActiveStep( FlowStep step );

signals:
    void stepClicked( FlowStep step );
    void openSourceRequested();
    void manageClassesRequested();
    void collectSamplesRequested();
    void evaluateRequested();
    void classifyRequested();
    void accuracyRequested();
    void postProcessRequested();
    void exportRequested();

public slots:
    void refreshState();

private:
    struct StepCard {
        FlowStep step;
        QFrame *cardFrame = nullptr;
        QLabel *numberLabel = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *statusBadge = nullptr;
        QLabel *detailLabel = nullptr;
        QLabel *metricLabel = nullptr;
        QPushButton *actionBtn = nullptr;
        bool isComplete = false;
        bool isActive = false;
    };

    void setupUi();
    QFrame *createStepCard( FlowStep step, const QString &num, const QString &title,
                            const QString &desc, const QString &actionText );
    void updateCardStyle( StepCard &card );
    void updateOverallProgress();

    RsClassifyWorkflowController *m_controller = nullptr;
    FlowStep m_activeStep = FlowStep::SourceRaster;

    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_cardContainer = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;

    QVector<StepCard> m_cards;

    // Metrics cache
    QString m_sourceRasterText = QStringLiteral( tr("No image loaded") );
    QString m_classCountText = QStringLiteral( tr("Undefined class") );
    QString m_sampleText = QStringLiteral( tr("0 ROIs, 0 pixels") );
    QString m_evalText = QStringLiteral( tr("Not assessed") );
    QString m_classifyText = QStringLiteral( tr("Unclassified") );
    QString m_accuracyText = QStringLiteral( tr("Accuracy not assessed") );
    QString m_postText = QStringLiteral( tr("No post-processing applied") );
    QString m_exportText = QStringLiteral( tr("Not exported") );
    bool m_hasSource = false;
};
