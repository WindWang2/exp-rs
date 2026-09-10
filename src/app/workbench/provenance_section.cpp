/***************************************************************************
 * provenance_section.cpp — see provenance_section.h
 ***************************************************************************/
#include "provenance_section.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QVBoxLayout>

#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <QDateTime>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/governance/governance_store.h"
#include "data/governance/workspace_service.h"

namespace sicnu::app
{
namespace
{
constexpr int kMaxTargets = 4;    // multi-selection summary stays bounded
constexpr int kMaxChainDepth = 6; // ancestor walk bound (cycle-safe via visited set)
constexpr int kMaxParamChars = 1200;

QString escapeCell( const QString &text )
{
    return text.toHtmlEscaped();
}

QString row( const QString &key, const QString &value )
{
    return QStringLiteral( "<tr><td><b>%1</b></td><td>%2</td></tr>" ).arg( escapeCell( key ), value );
}

QString section( const QString &title, const QString &rows )
{
    if ( rows.isEmpty() )
        return QString();
    return QStringLiteral( "<h3>%1</h3><table cellspacing='3'>%2</table>" ).arg( escapeCell( title ), rows );
}

QString prettyParams( const QJsonObject &parameters )
{
    if ( parameters.isEmpty() )
        return QString();
    QString text = QString::fromUtf8(
        QJsonDocument( parameters ).toJson( QJsonDocument::Indented ) );
    if ( text.size() > kMaxParamChars )
        text = text.left( kMaxParamChars ) + QObject::tr( "…（参数快照已截断）" );
    return QStringLiteral( "<code>%1</code>" ).arg( escapeCell( text ) );
}

QString warningLine( const QString &text )
{
    return QStringLiteral( "<span style='color:#b8860b'>⚠ %1</span><br/>" ).arg( escapeCell( text ) );
}

QString kindToString( sicnu::data::AssetKind kind )
{
    switch ( kind )
    {
        case sicnu::data::AssetKind::Raster:
            return QObject::tr( "栅格" );
        case sicnu::data::AssetKind::Vector:
            return QObject::tr( "矢量" );
        case sicnu::data::AssetKind::RemoteMap:
            return QObject::tr("远程地图");
        case sicnu::data::AssetKind::VirtualRaster:
            return QObject::tr( "虚拟栅格" );
    }
    return QObject::tr( "未知" );
}

QString stateToString( sicnu::data::AssetState state )
{
    switch ( state )
    {
        case sicnu::data::AssetState::Registered:
            return QObject::tr( "已注册" );
        case sicnu::data::AssetState::Resolving:
            return QObject::tr( "解析中" );
        case sicnu::data::AssetState::Ready:
            return QObject::tr( "就绪" );
        case sicnu::data::AssetState::Missing:
            return QObject::tr( "源文件缺失" );
        case sicnu::data::AssetState::UnavailableSource:
            return QObject::tr( "输入不可用" );
        case sicnu::data::AssetState::Offline:
            return QObject::tr( "离线" );
        case sicnu::data::AssetState::AuthenticationRequired:
            return QObject::tr( "需要认证" );
        case sicnu::data::AssetState::Error:
            return QObject::tr( "错误" );
        case sicnu::data::AssetState::Stale:
            return QObject::tr( "内容已过期" );
    }
    return QObject::tr( "未知" );
}

bool isWarningState( sicnu::data::AssetState state )
{
    switch ( state )
    {
        case sicnu::data::AssetState::Missing:
        case sicnu::data::AssetState::UnavailableSource:
        case sicnu::data::AssetState::Error:
        case sicnu::data::AssetState::Stale:
            return true;
        default:
            return false;
    }
}

} // namespace

ProvenanceSection::ProvenanceSection( DataManagerProvider dataManager,
                                      WorkspaceServiceProvider workspace, QWidget *parent )
    : InspectorSection( parent ), m_dataManager( std::move( dataManager ) ),
      m_workspace( std::move( workspace ) )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 8, 8, 8, 8 );
    m_body = new QLabel( this );
    m_body->setObjectName( QStringLiteral( "rsInspectorProvenance" ) );
    m_body->setTextFormat( Qt::RichText );
    m_body->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    m_body->setWordWrap( true );
    layout->addWidget( m_body );
    layout->addStretch( 1 );
}

bool ProvenanceSection::supports( const SelectionContextSnapshot &snapshot ) const
{
    // The provider may legitimately return null (project context not ready) —
    // the decision needs the RESULT, not just the callable.
    if ( !m_dataManager || !m_dataManager() )
        return false;
    if ( snapshot.hasGovernanceSelection() )
        return true;
    return snapshot.hasLayerSelection();
}

void ProvenanceSection::populate( const SelectionContextSnapshot &snapshot )
{
    sicnu::data::DataManager *dataManager = m_dataManager ? m_dataManager() : nullptr;
    sicnu::workspace::WorkspaceService *workspace = m_workspace ? m_workspace() : nullptr;
    if ( !dataManager )
    {
        m_body->setText( tr( "数据目录不可用，无法显示溯源。" ) );
        return;
    }

    // ── Resolve targets (bounded) ────────────────────────────────────────
    QVector<sicnu::data::AssetId> targets;
    QStringList targetWarnings;
    auto pushTarget = [&]( const sicnu::data::AssetId &id ) {
        if ( id.isNull() || targets.contains( id ) || targets.size() >= kMaxTargets )
            return;
        targets.append( id );
    };

    // 1) Data Manager asset selection.
    for ( const QString &idText : snapshot.selectedAssetIds )
    {
        if ( const auto id = sicnu::data::AssetId::fromString( idText ) )
            pushTarget( *id );
    }

    // 2) Governance Results/History selection → GovernedAsset → asset id.
    if ( workspace && targets.isEmpty() )
    {
        for ( const QString &entityId : snapshot.selectedResultIds )
        {
            if ( targets.size() >= kMaxTargets )
                break;
            const std::optional<sicnu::workspace::GovernedAsset> governed =
                workspace->store().assetById( entityId );
            if ( !governed )
                continue;
            if ( const auto id = sicnu::data::AssetId::fromString( governed->assetId ) )
                pushTarget( *id );
            else if ( !governed->canonicalSource.isEmpty() )
                if ( const auto byPath = dataManager->findByPath( governed->canonicalSource ) )
                    pushTarget( byPath->id() );
        }
    }

    // 3) Layer selection: the layer's source path through the catalog.
    if ( targets.isEmpty() )
    {
        QList<QgsMapLayer *> layers;
        if ( snapshot.activeLayer )
            layers.append( snapshot.activeLayer );
        for ( QgsMapLayer *layer : snapshot.selectedLayers )
            if ( layer && layers.size() < kMaxTargets )
                layers.append( layer );
        for ( QgsMapLayer *layer : layers )
        {
            if ( targets.size() >= kMaxTargets )
                break;
            if ( !layer || layer->source().isEmpty() )
                continue;
            const std::optional<sicnu::data::AssetSnapshot> asset =
                dataManager->findByPath( layer->source() );
            if ( asset )
                pushTarget( asset->id() );
        }
    }

    if ( targets.isEmpty() )
    {
        m_body->setText( tr( "当前选中项未注册到数据目录，没有平台溯源信息。" ) );
        return;
    }

    // ── Render each target ───────────────────────────────────────────────
    QString html;
    const int selectedEntities =
        snapshot.selectedAssetIds.size() + snapshot.selectedResultIds.size();
    const int selectedLayers =
        snapshot.selectedLayers.size() + ( snapshot.activeLayer ? 1 : 0 );
    if ( selectedEntities > kMaxTargets || selectedLayers > kMaxTargets )
        html += warningLine( tr( "多选 — 仅显示前 %1 项的溯源。" ).arg( targets.size() ) );

    for ( const sicnu::data::AssetId &id : targets )
    {
        const std::optional<sicnu::data::AssetSnapshot> asset = dataManager->asset( id );
        if ( !asset )
        {
            html += warningLine( tr( "资产 %1 已不在数据目录中。" ).arg( id.toString() ) );
            continue;
        }

        QString identity;
        identity += row( tr( "名称" ), escapeCell( asset->displayName() ) );
        identity += row( tr( "资产 ID" ), id.toString() );
        identity += row( tr( "类型" ), kindToString( asset->kind() ) );
        identity += row( tr( "版本" ), tr( "r%1" ).arg( asset->revision().value() ) );
        if ( isWarningState( asset->state() ) )
            identity += row( tr( "状态" ),
                             QStringLiteral( "<span style='color:#b8860b'>%1</span>" )
                                 .arg( stateToString( asset->state() ) ) );
        else
            identity += row( tr( "状态" ), stateToString( asset->state() ) );
        html += section( tr( "资产标识" ), identity );

        QString quality;
        if ( isWarningState( asset->state() ) )
            quality += warningLine(
                tr( "资产状态为「%1」——结果可能不可读或与目录不一致。" )
                    .arg( stateToString( asset->state() ) ) );

        const std::optional<sicnu::data::DerivationRecord> record = dataManager->provenance( id );
        QString provenance;
        if ( record )
        {
            provenance += row( tr( "算子 / 模型" ), escapeCell( record->algorithmId ) );
            if ( !record->algorithmVersion.isEmpty() )
                provenance += row( tr( "算子版本" ), escapeCell( record->algorithmVersion ) );
            // Workflow/run linkage (goal §B): workflow fields win when present;
            // otherwise the producing task reference is the run identity.
            if ( !record->workflowId.isEmpty() || !record->workflowRunId.isEmpty() )
            {
                provenance += row( tr( "工作流" ), escapeCell( record->workflowId ) );
                provenance += row( tr( "运行" ), escapeCell( record->workflowRunId ) );
                if ( !record->stepId.isEmpty() )
                    provenance += row( tr( "步骤" ), escapeCell( record->stepId ) );
            }
            else if ( !record->taskReference.isEmpty() )
            {
                provenance += row( tr( "任务引用" ), escapeCell( record->taskReference ) );
            }

            if ( record->completedAtUtc.isValid() )
                provenance +=
                    row( tr( "完成时间" ), record->completedAtUtc.toLocalTime().toString( Qt::ISODate ) );

            const QString params = prettyParams( record->parameters );
            if ( !params.isEmpty() )
                provenance += row( tr( "参数快照" ), params );

            // Verification (goal §B): execution fingerprint + cache truth.
            if ( !record->executionFingerprint.isEmpty() )
                provenance += row( tr( "执行指纹" ),
                                   QStringLiteral( "<code>%1</code>" )
                                       .arg( escapeCell( record->executionFingerprint ) ) );
            provenance += row( tr( "缓存" ), record->cacheHit ? tr( "命中（未重新计算）" )
                                                              : tr( "未命中（新计算）" ) );

            // Source assets with their exact revisions.
            if ( !record->inputs.isEmpty() )
            {
                QString inputRows;
                for ( const sicnu::data::DerivationInput &input : record->inputs )
                {
                    const std::optional<sicnu::data::AssetSnapshot> inputAsset =
                        dataManager->asset( input.assetId );
                    const QString name =
                        inputAsset ? inputAsset->displayName() : input.assetId.toString();
                    inputRows += QStringLiteral( "%1（r%2）<br/>" )
                                     .arg( escapeCell( name ), QString::number( input.revision.value() ) );
                    if ( !inputAsset )
                        quality += warningLine(
                            tr( "输入资产 %1 已从目录中删除，链条不完整。" )
                                .arg( input.assetId.toString() ) );
                }
                provenance += row( tr( "源资产" ), inputRows );
            }
            for ( const QString &unresolved : record->unresolvedInputPaths )
                quality += warningLine(
                    tr( "输入路径 %1 未能解析为注册资产（未注册或拼写差异）。" )
                        .arg( escapeCell( unresolved ) ) );

            if ( record->collectionId )
                provenance += row( tr( "时序集合" ),
                                   QStringLiteral( "%1（r%2）" )
                                       .arg( record->collectionId->toString(),
                                             QString::number( record->collectionRevision ) ) );
        }
        else
        {
            provenance += row( tr( "溯源" ), tr( "无派生记录（直接注册）" ) );
        }
        html += section( tr( "生产过程" ), provenance );

        // Derivation chain: bounded ancestor walk through the authoritative
        // edges. Renders "self ← parent ← grandparent"; cycles are impossible
        // in the store but the visited set keeps the walk safe regardless.
        QVector<sicnu::data::AssetId> chain;
        QSet<QString> visited;
        visited.insert( id.toString() );
        sicnu::data::AssetId cursor = id;
        bool truncated = false;
        for ( int depth = 0; depth < kMaxChainDepth; ++depth )
        {
            const QVector<sicnu::data::AssetId> parents = dataManager->derivedFrom( cursor );
            if ( parents.size() > 1 )
                truncated = true; // multi-input derivation: one branch shown
            bool advanced = false;
            for ( const sicnu::data::AssetId &parent : parents )
            {
                if ( visited.contains( parent.toString() ) )
                    continue;
                chain.append( parent );
                visited.insert( parent.toString() );
                cursor = parent;
                advanced = true;
                break;
            }
            if ( !advanced )
                break;
            if ( depth == kMaxChainDepth - 1 )
                truncated = true;
        }
        if ( !chain.isEmpty() || truncated )
        {
            QString chainText = escapeCell( asset->displayName() );
            for ( const sicnu::data::AssetId &ancestor : chain )
            {
                const std::optional<sicnu::data::AssetSnapshot> ancestorAsset =
                    dataManager->asset( ancestor );
                chainText += tr( " ← %1" )
                                     .arg( escapeCell( ancestorAsset ? ancestorAsset->displayName()
                                                                     : ancestor.toString() ) );
            }
            if ( truncated )
                chainText += tr( " ← …（链已截断）" );
            html += section( tr( "派生链" ),
                             row( tr( "自源至此" ), chainText ) );
        }

        const QVector<sicnu::data::AssetId> outputs = dataManager->derivedOutputsOf( id );
        if ( !outputs.isEmpty() )
        {
            QString outputRows;
            int shown = 0;
            for ( const sicnu::data::AssetId &output : outputs )
            {
                if ( shown >= kMaxTargets )
                {
                    outputRows += tr( "…共 %1 项派生产物" ).arg( outputs.size() );
                    break;
                }
                const std::optional<sicnu::data::AssetSnapshot> outputAsset =
                    dataManager->asset( output );
                outputRows += escapeCell( outputAsset ? outputAsset->displayName()
                                                      : output.toString() )
                              + QStringLiteral( "<br/>" );
                ++shown;
            }
            html += section( tr( "派生产物" ), row( tr( "被用于" ), outputRows ) );
        }

        // Governance verification enrichment (goal §B verification/quality).
        if ( workspace )
        {
            QString verification;
            const std::optional<sicnu::workspace::GovernedAsset> governed =
                workspace->store().assetById( id.toString() );
            if ( governed )
            {
                const QString availability = governed->availability.isEmpty()
                                                 ? tr( "未知" )
                                                 : governed->availability;
                verification += row( tr( "目录可用性" ), escapeCell( availability ) );
                if ( governed->availability == QLatin1String( "stale" ) )
                    quality += warningLine( tr( "治理目录标记该资产为过期（stale）。" ) );
                else if ( governed->availability == QLatin1String( "unverified" ) )
                    quality += warningLine( tr( "治理目录尚未校验该资产（unverified）。" ) );
                verification += row(
                    tr( "内容指纹" ),
                    governed->contentFingerprint.isEmpty()
                        ? tr( "未计算" )
                        : QStringLiteral( "<code>%1</code>" )
                              .arg( escapeCell( governed->contentFingerprint ) ) );
                verification += row(
                    tr( "完整性校验" ),
                    governed->verifiedMs > 0
                        ? QDateTime::fromMSecsSinceEpoch( governed->verifiedMs ).toString(
                              Qt::ISODate )
                        : tr( "从未校验" ) );
                if ( governed->verifiedMs == 0 )
                    quality += warningLine( tr( "该资产从未做过完整性校验。" ) );
                if ( !governed->modality.isEmpty() || !governed->sensor.isEmpty() )
                    verification += row( tr( "载荷 / 传感器" ),
                                         escapeCell( QStringList{ governed->modality, governed->sensor }
                                                         .join( QLatin1Char( '/' ) ) ) );
                html += section( tr( "校验与治理" ), verification );
            }
        }

        html += section( tr( "质量提示" ), quality );
        html += QStringLiteral( "<hr/>" );
    }

    m_body->setText( html );
}

} // namespace sicnu::app
