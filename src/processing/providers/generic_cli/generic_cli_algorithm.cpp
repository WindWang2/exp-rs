// generic_cli_algorithm.cpp — Generic CLI tool wrapper for user-defined tools
#include "generic_cli_algorithm.h"
#include "tools/tool_path_manager.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

#include <qgsapplication.h>
#include <qgsexception.h>
#include <qgsmessagelog.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

namespace
{
QString paramTypeForName( const QJsonArray &params, const QString &name )
{
    for ( const QJsonValue &paramVal : params )
    {
        const QJsonObject param = paramVal.toObject();
        if ( param.value( QStringLiteral( "name" ) ).toString() == name )
            return param.value( QStringLiteral( "type" ) ).toString();
    }
    return QString();
}
} // namespace

GenericCliAlgorithm::GenericCliAlgorithm(const QJsonObject &config, const QString &providerId)
    : m_config(config)
    , m_providerId(providerId)
{
}

QString GenericCliAlgorithm::name() const
{
    return m_config.value("id").toString();
}

QString GenericCliAlgorithm::displayName() const
{
    return m_config.value("name").toString();
}

QString GenericCliAlgorithm::group() const
{
    return m_config.value("group").toString("Custom Tools");
}

QString GenericCliAlgorithm::groupId() const
{
    return m_config.value("group_id").toString("custom");
}

QStringList GenericCliAlgorithm::tags() const
{
    QStringList tags;
    QJsonArray tagArray = m_config.value("tags").toArray();
    for (const QJsonValue &tag : tagArray)
        tags.append(tag.toString());
    return tags;
}

QgsProcessingAlgorithm *GenericCliAlgorithm::createInstance() const
{
    return new GenericCliAlgorithm(m_config, m_providerId);
}

void GenericCliAlgorithm::initAlgorithm(const QVariantMap &)
{
    QJsonArray params = m_config.value("parameters").toArray();
    for (const QJsonValue &paramVal : params) {
        QJsonObject param = paramVal.toObject();
        QString name = param.value("name").toString();
        QString type = param.value("type").toString();
        QString desc = param.value("description").toString();
        QJsonValue defaultVal = param.value("default");

        if (type == "raster") {
            addParameter(new QgsProcessingParameterRasterLayer(name, desc));
        } else if (type == "vector") {
            addParameter(new QgsProcessingParameterVectorLayer(name, desc));
        } else if (type == "number") {
            double def = defaultVal.toDouble(0.0);
            addParameter(new QgsProcessingParameterNumber(name, desc, Qgis::ProcessingNumberParameterType::Double, def));
        } else if (type == "string") {
            addParameter(new QgsProcessingParameterString(name, desc, defaultVal.toString()));
        } else if (type == "boolean") {
            addParameter(new QgsProcessingParameterBoolean(name, desc, defaultVal.toBool()));
        } else if (type == "output_raster") {
            addParameter(new QgsProcessingParameterRasterDestination(name, desc));
        } else if (type == "output_vector") {
            addParameter(new QgsProcessingParameterFeatureSink(name, desc));
        }
    }
}

QString GenericCliAlgorithm::resolveCommand( const QString &command ) const
{
    if ( command.isEmpty() )
        return QString();

    if ( QFileInfo::exists( command ) )
        return command;

    if ( command.startsWith( QStringLiteral( "otbcli_" ) ) )
    {
        const QString appName = command.mid( 7 );
        const QString resolved = ToolPathManager::instance().otbToolPath( appName );
        if ( !resolved.isEmpty() )
            return resolved;
    }

    const QString gdalResolved = ToolPathManager::instance().gdalToolPath( command );
    if ( !gdalResolved.isEmpty() )
        return gdalResolved;

    const QString resolved = QStandardPaths::findExecutable( command );
    return resolved.isEmpty() ? command : resolved;
}

QString GenericCliAlgorithm::resolveParameterValue( const QString &paramName,
                                                    const QVariant &value,
                                                    QgsProcessingContext &context ) const
{
    const QJsonArray params = m_config.value( QStringLiteral( "parameters" ) ).toArray();
    const QString type = paramTypeForName( params, paramName );

    if ( type == QStringLiteral( "raster" ) )
    {
        if ( QgsRasterLayer *layer = parameterAsRasterLayer( { { paramName, value } }, paramName, context ) )
            return layer->source();
        return value.toString();
    }

    if ( type == QStringLiteral( "vector" ) )
    {
        if ( QgsVectorLayer *layer = parameterAsVectorLayer( { { paramName, value } }, paramName, context ) )
            return layer->source();
        return value.toString();
    }

    if ( type == QStringLiteral( "output_raster" ) )
        return parameterAsOutputLayer( { { paramName, value } }, paramName, context );

    if ( type == QStringLiteral( "output_vector" ) )
        return parameterAsOutputLayer( { { paramName, value } }, paramName, context );

    if ( type == QStringLiteral( "boolean" ) )
        return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );

    if ( type == QStringLiteral( "number" ) )
        return QString::number( value.toDouble() );

    return value.toString();
}

QStringList GenericCliAlgorithm::buildArgs( const QVariantMap &parameters,
                                            QgsProcessingContext &context,
                                            QgsProcessingFeedback *feedback ) const
{
    QStringList args;
    QJsonArray argTemplate = m_config.value("args").toArray();

    for (const QJsonValue &argVal : argTemplate) {
        QString arg = argVal.toString();

        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            QString placeholder = "{" + it.key() + "}";
            if (arg.contains(placeholder)) {
                arg.replace(placeholder, resolveParameterValue(it.key(), it.value(), context));
            }
        }

        if (arg.contains("{") && arg.contains("}")) {
            if (feedback)
                feedback->pushWarning(QObject::tr("Skipping argument with unresolved placeholder: %1").arg(arg));
            continue;
        }

        args.append(arg);
    }

    if ( m_config.value( QStringLiteral( "append_extra" ) ).toBool() )
    {
        const QString extra = parameters.value( QStringLiteral( "EXTRA" ) ).toString().trimmed();
        if ( !extra.isEmpty() )
            args << QProcess::splitCommand( extra );
    }

    return args;
}

QVariantMap GenericCliAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                   QgsProcessingContext &context,
                                                   QgsProcessingFeedback *feedback)
{
    const QString rawCommand = m_config.value("command").toString();
    if (rawCommand.isEmpty()) {
        const QString err = QObject::tr("No command specified in tool configuration");
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    const QString command = resolveCommand( rawCommand );
    if ( !QFileInfo::exists( command ) && QStandardPaths::findExecutable( command ).isEmpty() )
    {
        QString err;
        if ( rawCommand.startsWith( QStringLiteral( "otbcli_" ) ) )
        {
            err = QObject::tr(
                "OTB application '%1' not found.\n"
                "Install OTB or set the OTB path in Preferences (Tools tab) or SICNU_OTB_PATH." )
                      .arg( rawCommand );
        }
        else
        {
            err = QObject::tr( "Command '%1' not found. Ensure the tool is installed and on PATH." )
                      .arg( rawCommand );
        }
        if (feedback)
            feedback->reportError( err );
        QgsMessageLog::logMessage( err, QStringLiteral( "generic_cli" ), Qgis::MessageLevel::Critical );
        throw QgsProcessingException(err);
    }

    // Resolve destinations ONCE. parameterAsOutputLayer(TEMPORARY_OUTPUT) generates a
    // new unique path on every call — re-resolving for results must not create a second path.
    QVariantMap resolvedParameters = parameters;
    QJsonArray paramDefs = m_config.value( QStringLiteral( "parameters" ) ).toArray();
    for ( const QJsonValue &paramVal : paramDefs )
    {
        const QJsonObject param = paramVal.toObject();
        const QString name = param.value( QStringLiteral( "name" ) ).toString();
        const QString type = param.value( QStringLiteral( "type" ) ).toString();
        if ( !type.startsWith( QStringLiteral( "output_" ) ) || !resolvedParameters.contains( name ) )
            continue;
        resolvedParameters.insert(
          name, resolveParameterValue( name, resolvedParameters.value( name ), context ) );
    }

    QStringList args = buildArgs(resolvedParameters, context, feedback);
    if (args.isEmpty() && !m_config.value(QStringLiteral("args")).toArray().isEmpty()) {
        const QString err = QObject::tr("Failed to build arguments for tool '%1'.").arg(name());
        if (feedback)
            feedback->reportError(err);
        throw QgsProcessingException(err);
    }

    QString cmdLine = command + " " + args.join(" ");
    if (feedback)
        feedback->pushInfo(QObject::tr("Running: %1").arg(cmdLine));
    QgsMessageLog::logMessage(cmdLine, "generic_cli", Qgis::MessageLevel::Info);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);

    if ( rawCommand.startsWith( QStringLiteral( "otbcli" ) ) )
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if ( const QString bundleDir = ToolPathManager::instance().otbBundleDir(); !bundleDir.isEmpty() )
        {
            env.insert( QStringLiteral( "OTB_APPLICATION_PATH" ),
                        QDir( bundleDir ).filePath( QStringLiteral( "lib/otb/applications" ) ) );
            const QString binPath = QDir( bundleDir ).filePath( QStringLiteral( "bin" ) );
            const QString libPath = QDir( bundleDir ).filePath( QStringLiteral( "lib" ) );
            const QString path = env.value( QStringLiteral( "PATH" ) );
            env.insert( QStringLiteral( "PATH" ), binPath + ( path.isEmpty() ? QString() : QStringLiteral( ":" ) + path ) );
            const QString ldPath = env.value( QStringLiteral( "LD_LIBRARY_PATH" ) );
            env.insert( QStringLiteral( "LD_LIBRARY_PATH" ),
                        libPath + ( ldPath.isEmpty() ? QString() : QStringLiteral( ":" ) + ldPath ) );
            env.insert( QStringLiteral( "LC_NUMERIC" ), QStringLiteral( "C" ) );
        }
        proc.setProcessEnvironment( env );
    }

    proc.start(command, args);

    if (!proc.waitForStarted(5000)) {
        QString err = QObject::tr("Failed to start tool: %1").arg(proc.errorString());
        if (feedback)
            feedback->reportError(err);
        QgsMessageLog::logMessage(err, "generic_cli", Qgis::MessageLevel::Critical);
        throw QgsProcessingException(err);
    }

    while (proc.state() == QProcess::Running) {
        if (feedback && feedback->isCanceled()) {
            proc.kill();
            const QString err = QObject::tr("Tool execution canceled by user.");
            feedback->reportError(err);
            throw QgsProcessingException(err);
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            QString msg = QString::fromUtf8(output);
            if (feedback) feedback->pushInfo(msg);
            QgsMessageLog::logMessage(msg, "generic_cli", Qgis::MessageLevel::Info);
        }
    }

    if (proc.exitCode() != 0) {
        QString err = QObject::tr("Tool failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardOutput()));
        if (feedback) feedback->reportError(err);
        QgsMessageLog::logMessage(err, "generic_cli", Qgis::MessageLevel::Warning);
        throw QgsProcessingException(err);
    }

    QVariantMap results;
    for ( const QJsonValue &paramVal : paramDefs )
    {
        const QJsonObject param = paramVal.toObject();
        const QString name = param.value( QStringLiteral( "name" ) ).toString();
        const QString type = param.value( QStringLiteral( "type" ) ).toString();
        if ( !type.startsWith( QStringLiteral( "output_" ) ) || !resolvedParameters.contains( name ) )
            continue;

        const QString outPath = resolvedParameters.value( name ).toString();
        if ( outPath.isEmpty() )
            continue;

        if ( !QFileInfo::exists( outPath ) )
        {
            const QString err = QObject::tr(
                                  "Tool reported success but output file is missing: %1" )
                                  .arg( outPath );
            if ( feedback )
                feedback->reportError( err );
            QgsMessageLog::logMessage( err, QStringLiteral( "generic_cli" ), Qgis::MessageLevel::Critical );
            throw QgsProcessingException(err);
        }

        results[name] = outPath;
    }

    return results;
}
