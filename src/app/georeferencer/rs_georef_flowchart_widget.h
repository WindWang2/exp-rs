// rs_georef_flowchart_widget.h — Interactive flowchart panel for Geometric Correction workflow.
#pragma once

#include "rs_georeferencing_session.h"

#include <QWidget>
#include <QVector>

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QFrame;

/**
 * \brief Interactive flowchart panel displaying the 7-stage remote sensing geometric correction pipeline.
 *
 * Shows the full end-to-end workflow:
 * 1. 加载源影像 (Load Source Image)
 * 2. 采集控制点 (Collect GCPs)
 * 3. 变换模型选择 (Transformation Model)
 * 4. 残差与精度检查 (Residual & Accuracy Check)
 * 5. 校正参数配置 (Warp Configuration)
 * 6. 执行重采样校正 (Execute Resampling & Warp)
 * 7. 成果加载与验证 (Result Loading & Verification)
 *
 * Provides real-time status tracking, GCP & RMS metric badges, and interactive step navigation.
 */
class RsGeorefFlowchartWidget : public QWidget
{
    Q_OBJECT

public:
    enum class FlowStep {
        LoadSource = 0,
        CollectGcps,
        SelectModel,
        CheckResiduals,
        ConfigureWarp,
        ExecuteWarp,
        VerifyResult,
        Count
    };

    explicit RsGeorefFlowchartWidget( QWidget *parent = nullptr );
    ~RsGeorefFlowchartWidget() override = default;

    /// Bind to georeferencing session for real-time synchronization
    void bindSession( RsGeoreferencingSession *session );

    /// Update dynamic metric summaries
    void setSourceRasterInfo( const QString &sourcePath, int width, int height, int bands );
    void setGcpInfo( int totalGcps, int enabledGcps );
    void setModelInfo( const QString &methodName, int minGcpRequired );
    void setResidualInfo( double rmsPixels, bool isFitReady, const QString &statusText );
    void setWarpConfigInfo( const QString &destCrs, const QString &resampling, double pixelSize );
    void setWarpExecutionInfo( bool isRunning, const QString &statusText );
    void setOutputInfo( const QString &outputPath, bool isLoaded );

    /// Current active step
    FlowStep activeStep() const { return m_activeStep; }
    void setActiveStep( FlowStep step );

signals:
    void stepClicked( FlowStep step );
    void openSourceRequested();
    void collectGcpsRequested();
    void selectModelRequested();
    void checkResidualsRequested();
    void configWarpRequested();
    void executeWarpRequested();
    void loadResultRequested();

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

    RsGeoreferencingSession *m_session = nullptr;
    FlowStep m_activeStep = FlowStep::LoadSource;

    QProgressBar *m_progressBar = nullptr;
    QLabel *m_progressLabel = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_cardContainer = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;

    QVector<StepCard> m_cards;

    // Metrics cache
    QString m_sourceText = QStringLiteral( "未加载影像" );
    QString m_gcpText = QStringLiteral( "0 个 GCP (启用 0)" );
    QString m_modelText = QStringLiteral( "多项式 1 阶 (需 ≥3 点)" );
    QString m_residualText = QStringLiteral( "未解算" );
    QString m_warpConfigText = QStringLiteral( "未配置输出参数" );
    QString m_warpExecText = QStringLiteral( "未开始校正" );
    QString m_verifyText = QStringLiteral( "未加载成果" );
    bool m_hasSource = false;
    bool m_hasOutput = false;
};
