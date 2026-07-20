// src/processing/providers/otb_tools/otb_tool_wrapper.cpp
#include "otb_tool_wrapper.h"
#include "tools/tool_path_manager.h"

#include "core/sicnu_logging.h"
#include <qgsapplication.h>
#include <qgsexception.h>
#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <processing/qgsprocessingparameters.h>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace
{
QString shellQuote( const QString &arg )
{
    if ( arg.isEmpty() )
        return QStringLiteral( "''" );
    static const QRegularExpression needQuote( QStringLiteral( R"([^\w@%+=:,./-])" ) );
    if ( !arg.contains( needQuote ) )
        return arg;
    QString out = arg;
    out.replace( QLatin1Char( '\'' ), QStringLiteral( "'\\''" ) );
    return QLatin1Char( '\'' ) + out + QLatin1Char( '\'' );
}

QString joinCommandLine( const QString &program, const QStringList &args )
{
    QStringList parts;
    parts << shellQuote( program );
    for ( const QString &a : args )
        parts << shellQuote( a );
    return parts.join( QLatin1Char( ' ' ) );
}

QVariantMap resolveDestinationParameters( const QgsProcessingAlgorithm *algorithm,
                                          const QVariantMap &parameters,
                                          QgsProcessingContext &context )
{
    if ( !algorithm )
        return parameters;

    QVariantMap resolved = parameters;
    for ( const QgsProcessingParameterDefinition *param : algorithm->parameterDefinitions() )
    {
        if ( !param || !resolved.contains( param->name() ) )
            continue;

        const QString type = param->type();
        if ( type != QgsProcessingParameterRasterDestination::typeName()
             && type != QgsProcessingParameterVectorDestination::typeName()
             && type != QgsProcessingParameterFeatureSink::typeName() )
            continue;

        resolved.insert(
            param->name(),
            QgsProcessingParameters::parameterAsOutputLayer( param, resolved.value( param->name() ), context, true ) );
    }
    return resolved;
}
} // namespace

QVariantMap OtbToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    const QVariantMap resolvedParameters = resolveDestinationParameters( this, parameters, context );

    QString program = ToolPathManager::instance().otbToolPath(applicationName());
    if (program.isEmpty()) {
        const QString err = QObject::tr(
            "OTB application '%1' not found.\n"
            "Expected at: tools/otb/otbcli_%1\n"
            "Set SICNU_OTB_PATH environment variable or configure OTB path in Preferences.")
            .arg(applicationName());
        SICNU_LOG_ERROR( SicnuLogTags::OTB, err );
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    QStringList args = buildArgs(resolvedParameters, context, feedback);
    if (args.isEmpty()) {
        const QString err = QObject::tr("Failed to build arguments for OTB application '%1'.").arg(applicationName());
        SICNU_LOG_ERROR( SicnuLogTags::OTB, err );
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    SICNU_LOG_INFO( SicnuLogTags::OTB, QString( "Executing OTB application: %1" ).arg( applicationName() ) );
    if (!runOtbApplication(program, args, feedback)) {
        const QString err = QObject::tr("OTB application '%1' failed").arg(applicationName());
        SICNU_LOG_ERROR( SicnuLogTags::OTB, err );
        throw QgsProcessingException(err);
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::OTB, QString( "OTB application '%1' completed successfully" ).arg( applicationName() ) );
    QVariantMap results;
    for ( const QgsProcessingParameterDefinition *param : parameterDefinitions() )
    {
        if ( !param || !resolvedParameters.contains( param->name() ) )
            continue;

        const QString type = param->type();
        if ( type == QgsProcessingParameterRasterDestination::typeName()
             || type == QgsProcessingParameterVectorDestination::typeName()
             || type == QgsProcessingParameterFeatureSink::typeName() )
        {
            const QString outPath = resolvedParameters.value( param->name() ).toString();
            if ( !outPath.isEmpty() && !QFileInfo::exists( outPath ) )
            {
                const QString err = QObject::tr(
                                      "Tool reported success but output file is missing: %1" )
                                      .arg( outPath );
                if ( feedback )
                    feedback->reportError( err );
                SICNU_LOG_ERROR( SicnuLogTags::OTB, err );
                throw QgsProcessingException(err);
            }
            results.insert( param->name(), outPath );
        }
    }
    return results;
}

QString OtbToolWrapper::commandLinePreview( const QVariantMap &parameters,
                                            QgsProcessingContext &context )
{
    const QVariantMap resolved = resolveDestinationParameters( this, parameters, context );
    QString program = ToolPathManager::instance().otbToolPath( applicationName() );
    if ( program.isEmpty() )
        program = QStringLiteral( "otbcli_%1" ).arg( applicationName() );

    try
    {
        QgsProcessingFeedback feedback;
        const QStringList args = buildArgs( resolved, context, &feedback );
        if ( args.isEmpty() )
            return QObject::tr( "# 无法根据当前参数生成命令（请检查必填项）" );
        return joinCommandLine( program, args );
    }
    catch ( const QgsProcessingException &e )
    {
        return QObject::tr( "# 命令预览失败: %1" ).arg( e.what() );
    }
    catch ( ... )
    {
        return QObject::tr( "# 命令预览失败（参数不完整或无效）" );
    }
}

bool OtbToolWrapper::runOtbApplication(const QString &program, const QStringList &args, QgsProcessingFeedback *feedback)
{
    QString cmdLine = program + " " + args.join(" ");
    if (feedback) feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    SICNU_LOG_INFO( SicnuLogTags::OTB, cmdLine );

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if ( const QString bundleDir = ToolPathManager::instance().otbBundleDir(); !bundleDir.isEmpty() )
    {
        const QString appPath = QDir( bundleDir ).filePath( QStringLiteral( "lib/otb/applications" ) );
        const QString binPath = QDir( bundleDir ).filePath( QStringLiteral( "bin" ) );
        env.insert( QStringLiteral( "OTB_APPLICATION_PATH" ), appPath );
        const QString path = env.value( QStringLiteral( "PATH" ) );
        env.insert( QStringLiteral( "PATH" ), binPath + ( path.isEmpty() ? QString() : QStringLiteral( ":" ) + path ) );
        env.insert( QStringLiteral( "LC_NUMERIC" ), QStringLiteral( "C" ) );
    }
    proc.setProcessEnvironment( env );

    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        QString err = QObject::tr("Failed to start OTB application: %1").arg(proc.errorString());
        if (feedback) feedback->reportError(err);
        SICNU_LOG_ERROR( SicnuLogTags::OTB, err );
        return false;
    }

    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(QObject::tr("OTB application canceled by user."));
            return false;
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            QString msg = QString::fromUtf8(output);
            if (feedback) feedback->pushInfo(msg);
            SICNU_LOG_INFO( SicnuLogTags::OTB, msg );
        }
    }

    if (proc.exitCode() != 0) {
        QString err = QObject::tr("OTB application failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardOutput()));
        if (feedback) feedback->reportError(err);
        SICNU_LOG_WARN( SicnuLogTags::OTB, err );
        return false;
    }

    return true;
}

QString OtbToolWrapper::rasterLayerSource(const QVariant &var)
{
    if (var.canConvert<QgsRasterLayer *>()) {
        QgsRasterLayer *layer = var.value<QgsRasterLayer *>();
        return layer ? layer->source() : QString();
    }
    return var.toString();
}

QString OtbToolWrapper::vectorLayerSource(const QVariant &var)
{
    if (var.canConvert<QgsVectorLayer *>()) {
        QgsVectorLayer *layer = var.value<QgsVectorLayer *>();
        return layer ? layer->source() : QString();
    }
    return var.toString();
}
