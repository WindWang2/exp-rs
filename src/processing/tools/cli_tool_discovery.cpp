#include "cli_tool_discovery.h"
#include "tool_path_manager.h"

#include "app/app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>

namespace
{

QString humanizeCamelCase( const QString &name )
{
    QString out;
    out.reserve( name.size() + 8 );
    for ( int i = 0; i < name.size(); ++i )
    {
        const QChar c = name.at( i );
        if ( c.isUpper() && i > 0 )
            out += QLatin1Char( ' ' );
        out += c;
    }
    return out;
}

QString toSnakeCase( const QString &camel )
{
    QString out;
    out.reserve( camel.size() + 8 );
    for ( int i = 0; i < camel.size(); ++i )
    {
        const QChar c = camel.at( i );
        if ( c.isUpper() && i > 0 )
            out += QLatin1Char( '_' );
        out += c.toLower();
    }
    return out;
}

void collectExecutablesInDir( const QString &dirPath, QStringList &out, const QRegularExpression &namePattern )
{
    if ( dirPath.isEmpty() )
        return;

    QDir dir( dirPath );
    if ( !dir.exists() )
        return;

    const QFileInfoList entries = dir.entryInfoList( QDir::Files | QDir::Executable );
    for ( const QFileInfo &entry : entries )
    {
        const QString baseName = entry.fileName();
        if ( namePattern.match( baseName ).hasMatch() )
            out.append( baseName );
    }
}

QStringList uniqueSorted( QStringList items )
{
    items.removeDuplicates();
    items.sort();
    return items;
}

QStringList otbSearchDirectories()
{
    QStringList dirs;
    const ToolPathManager &mgr = ToolPathManager::instance();

    const QString bundleDir = mgr.otbBundleDir();
    if ( !bundleDir.isEmpty() )
        dirs << QDir( bundleDir ).filePath( QStringLiteral( "bin" ) );

    const QString appDir = QCoreApplication::applicationDirPath();
    for ( const QString &rel : {
              QStringLiteral( "tools/otb/bin" ),
              QStringLiteral( "tools/otb" ),
              QStringLiteral( "../tools/otb/bin" ),
              QStringLiteral( "../bin" ),
              QStringLiteral( "../../tools/otb/bin" ),
          } )
    {
        dirs << QDir( appDir ).filePath( rel );
    }

    dirs << QStringLiteral( "/usr/bin" );
    dirs << QStringLiteral( "/usr/local/bin" );

    QStringList cleaned;
    for ( const QString &d : dirs )
    {
        const QString path = QDir::cleanPath( d );
        if ( !cleaned.contains( path ) )
            cleaned << path;
    }
    return cleaned;
}

QStringList gdalSearchDirectories()
{
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();

    for ( const QString &rel : {
              QStringLiteral( "tools/gdal" ),
              QStringLiteral( "../tools/gdal" ),
          } )
    {
        dirs << QDir( appDir ).filePath( rel );
    }

    dirs << QStringLiteral( "/usr/bin" );
    dirs << QStringLiteral( "/usr/local/bin" );

    QStringList cleaned;
    for ( const QString &d : dirs )
    {
        const QString path = QDir::cleanPath( d );
        if ( !cleaned.contains( path ) )
            cleaned << path;
    }
    return cleaned;
}

bool isExcludedGdalTool( const QString &toolName )
{
    static const QSet<QString> excluded = {
        QStringLiteral( "gdal-config" ),
        QStringLiteral( "gdal-config.install" ),
        QStringLiteral( "gdal-config.uninstall" ),
    };
    return excluded.contains( toolName );
}

QSet<QString> customToolCommands()
{
    QSet<QString> commands;
    const QString shipped = AppPaths::resolveDataPath( QStringLiteral( "data/tools/custom" ) );
    QDir dir( shipped );
    if ( !dir.exists() )
        return commands;

    const QFileInfoList files = dir.entryInfoList( { QStringLiteral( "*.json" ) }, QDir::Files );
    for ( const QFileInfo &fileInfo : files )
    {
        QFile file( fileInfo.absoluteFilePath() );
        if ( !file.open( QIODevice::ReadOnly ) )
            continue;

        const QJsonObject obj = QJsonDocument::fromJson( file.readAll() ).object();
        const QString command = obj.value( QStringLiteral( "command" ) ).toString();
        if ( !command.isEmpty() )
            commands.insert( command );
    }
    return commands;
}

} // namespace

namespace CliToolDiscovery
{

QSet<QString> handcraftedOtbApplicationNames()
{
    return {
        QStringLiteral( "BandMath" ),
        QStringLiteral( "BandMathX" ),
        QStringLiteral( "BinaryMorphologicalOperation" ),
        QStringLiteral( "BundleToPerfectSensor" ),
        QStringLiteral( "ComputeImagesStatistics" ),
        QStringLiteral( "ConcatenateImages" ),
        QStringLiteral( "Convert" ),
        QStringLiteral( "DynamicConvert" ),
        QStringLiteral( "ExtractROI" ),
        QStringLiteral( "FeatureExtraction" ),
        QStringLiteral( "GrayLevelCooccurrenceMatrix" ),
        QStringLiteral( "GrayScaleMorphologicalOperation" ),
        QStringLiteral( "HaralickTextureExtraction" ),
        QStringLiteral( "ImageClassifier" ),
        QStringLiteral( "KMeansClassification" ),
        QStringLiteral( "LocalStatisticExtraction" ),
        QStringLiteral( "LSMS" ),
        QStringLiteral( "MeanShiftSmoothing" ),
        QStringLiteral( "MultiResolutionPyramid" ),
        QStringLiteral( "MultivariateAlterationDetector" ),
        QStringLiteral( "OrthoRectification" ),
        QStringLiteral( "PixelInfo" ),
        QStringLiteral( "RadiometricIndices" ),
        QStringLiteral( "ReadImageInfo" ),
        QStringLiteral( "Rescale" ),
        QStringLiteral( "Segmentation" ),
        QStringLiteral( "StereoRectificationGridGenerator" ),
        QStringLiteral( "Superimpose" ),
        QStringLiteral( "TrainImagesClassifier" ),
        QStringLiteral( "TrainVectorClassifier" ),
    };
}

QSet<QString> handcraftedGdalToolNames()
{
    return {
        QStringLiteral( "gdal_translate" ),
        QStringLiteral( "gdalwarp" ),
        QStringLiteral( "gdalinfo" ),
        QStringLiteral( "gdaldem" ),
        QStringLiteral( "gdal_contour" ),
        QStringLiteral( "gdal_polygonize" ),
        QStringLiteral( "gdal_merge" ),
        QStringLiteral( "gdal_calc" ),
        QStringLiteral( "gdal_retile" ),
        QStringLiteral( "gdal_proximity" ),
        QStringLiteral( "gdal_sieve" ),
        QStringLiteral( "gdal_fillnodata" ),
        QStringLiteral( "gdal_grid" ),
        QStringLiteral( "gdal_rasterize" ),
        QStringLiteral( "gdalbuildvrt" ),
        QStringLiteral( "gdaltindex" ),
        QStringLiteral( "gdalmanage" ),
        QStringLiteral( "gdaladdo" ),
        QStringLiteral( "gdaltransform" ),
        QStringLiteral( "gdal_edit" ),
        QStringLiteral( "pct2rgb" ),
        QStringLiteral( "rgb2pct" ),
        QStringLiteral( "gdal2xyz" ),
        QStringLiteral( "ogr2ogr" ),
        QStringLiteral( "ogrinfo" ),
        QStringLiteral( "ogrtindex" ),
    };
}

QStringList discoverOtbApplicationNames()
{
    static const QRegularExpression pattern( QStringLiteral( "^otbcli_(.+)$" ) );
    QStringList found;

    for ( const QString &dir : otbSearchDirectories() )
        collectExecutablesInDir( dir, found, pattern );

    QStringList apps;
    const QSet<QString> skip = handcraftedOtbApplicationNames();
    const QSet<QString> customCommands = customToolCommands();
    for ( const QString &fileName : uniqueSorted( found ) )
    {
        const QRegularExpressionMatch match = pattern.match( fileName );
        if ( !match.hasMatch() )
            continue;

        const QString appName = match.captured( 1 );
        if ( skip.contains( appName ) )
            continue;

        if ( customCommands.contains( QStringLiteral( "otbcli_" ) + appName ) )
            continue;

        if ( ToolPathManager::instance().otbToolPath( appName ).isEmpty() )
            continue;

        apps << appName;
    }

    return uniqueSorted( apps );
}

QStringList discoverGdalToolNames()
{
    static const QRegularExpression pattern( QStringLiteral( "^(gdal|ogr).+" ) );
    QStringList found;

    for ( const QString &dir : gdalSearchDirectories() )
        collectExecutablesInDir( dir, found, pattern );

    QStringList tools;
    const QSet<QString> skip = handcraftedGdalToolNames();
    const QSet<QString> customCommands = customToolCommands();
    for ( const QString &toolName : uniqueSorted( found ) )
    {
        // Normalize .py scripts (e.g. gdal_polygonize.py) against handcrafted basenames.
        QString baseName = toolName;
        if ( baseName.endsWith( QStringLiteral( ".py" ), Qt::CaseInsensitive ) )
            baseName.chop( 3 );

        if ( isExcludedGdalTool( toolName ) || isExcludedGdalTool( baseName )
             || skip.contains( toolName ) || skip.contains( baseName ) )
            continue;

        if ( customCommands.contains( toolName ) || customCommands.contains( baseName ) )
            continue;

        if ( ToolPathManager::instance().gdalToolPath( toolName ).isEmpty() )
            continue;

        tools << toolName;
    }

    return tools;
}

QString otbAlgorithmId( const QString &applicationName )
{
    return QStringLiteral( "otb_" ) + toSnakeCase( applicationName );
}

QString gdalAlgorithmId( const QString &toolName )
{
    return toolName;
}

QJsonObject makeOtbDiscoveredConfig( const QString &applicationName )
{
    QJsonObject config;
    config.insert( QStringLiteral( "id" ), otbAlgorithmId( applicationName ) );
    config.insert( QStringLiteral( "name" ), QStringLiteral( "OTB " ) + humanizeCamelCase( applicationName ) );
    config.insert( QStringLiteral( "group" ), QObject::tr( "OTB" ) );
    config.insert( QStringLiteral( "group_id" ), QStringLiteral( "otb" ) );
    config.insert( QStringLiteral( "command" ), QStringLiteral( "otbcli_" ) + applicationName );
    config.insert( QStringLiteral( "append_extra" ), true );
    config.insert( QStringLiteral( "tags" ), QJsonArray{
        QStringLiteral( "otb" ),
        QStringLiteral( "discovered" ),
        applicationName,
    } );

    QJsonArray parameters;
    parameters.append( QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "INPUT" ) },
        { QStringLiteral( "type" ), QStringLiteral( "raster" ) },
        { QStringLiteral( "description" ), QObject::tr( "Input raster (-in)" ) },
    } );
    parameters.append( QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "OUTPUT" ) },
        { QStringLiteral( "type" ), QStringLiteral( "output_raster" ) },
        { QStringLiteral( "description" ), QObject::tr( "Output raster (-out)" ) },
    } );
    parameters.append( QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "EXTRA" ) },
        { QStringLiteral( "type" ), QStringLiteral( "string" ) },
        { QStringLiteral( "description" ), QObject::tr( "Additional OTB parameters (see application -help)" ) },
    } );
    config.insert( QStringLiteral( "parameters" ), parameters );

    QJsonArray args;
    args.append( QStringLiteral( "-in" ) );
    args.append( QStringLiteral( "{INPUT}" ) );
    args.append( QStringLiteral( "-out" ) );
    args.append( QStringLiteral( "{OUTPUT}" ) );
    config.insert( QStringLiteral( "args" ), args );

    return config;
}

QJsonObject makeGdalDiscoveredConfig( const QString &toolName )
{
    const bool isVector = toolName.startsWith( QStringLiteral( "ogr" ) );
    const QString group = isVector ? QObject::tr( "Vector Conversion" ) : QObject::tr( "Raster Conversion" );
    const QString groupId = isVector ? QStringLiteral( "vectorconversion" ) : QStringLiteral( "rasterconversion" );

    QJsonObject config;
    config.insert( QStringLiteral( "id" ), gdalAlgorithmId( toolName ) );
    config.insert( QStringLiteral( "name" ), QStringLiteral( "GDAL " ) + toolName );
    config.insert( QStringLiteral( "group" ), group );
    config.insert( QStringLiteral( "group_id" ), groupId );
    config.insert( QStringLiteral( "command" ), toolName );
    config.insert( QStringLiteral( "append_extra" ), true );
    config.insert( QStringLiteral( "tags" ), QJsonArray{
        QStringLiteral( "gdal" ),
        QStringLiteral( "discovered" ),
        toolName,
    } );

    QJsonArray parameters;
    parameters.append( QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "INPUT" ) },
        { QStringLiteral( "type" ), isVector ? QStringLiteral( "vector" ) : QStringLiteral( "raster" ) },
        { QStringLiteral( "description" ), QObject::tr( "Input layer" ) },
    } );
    parameters.append( QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "OUTPUT" ) },
        { QStringLiteral( "type" ), isVector ? QStringLiteral( "output_vector" ) : QStringLiteral( "output_raster" ) },
        { QStringLiteral( "description" ), QObject::tr( "Output layer" ) },
    } );
    parameters.append( QJsonObject{
        { QStringLiteral( "name" ), QStringLiteral( "EXTRA" ) },
        { QStringLiteral( "type" ), QStringLiteral( "string" ) },
        { QStringLiteral( "description" ), QObject::tr( "Additional GDAL/OGR arguments" ) },
    } );
    config.insert( QStringLiteral( "parameters" ), parameters );

    QJsonArray args;
    args.append( QStringLiteral( "{INPUT}" ) );
    args.append( QStringLiteral( "{OUTPUT}" ) );
    config.insert( QStringLiteral( "args" ), args );

    return config;
}

} // namespace CliToolDiscovery