#include "output_verifier.h"

#include "core/qgsdatasourceresolver.h"
#include "data/asset_types.h"
#include "experiment/evaluation.h"
#include "geospatial/common.h"
#include "geospatial/raster/raster_reader.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <sstream>

namespace sicnu::agent
{

namespace
{

void addIssue( OutputVerification &report, const QString &message )
{
  report.issues.append( message );
  report.ok = false;
}

void addWarning( OutputVerification &report, const QString &message )
{
  report.warnings.append( message );
}

bool isValidGeoTransform( const double gt[6] )
{
  if ( std::isnan( gt[0] ) || std::isnan( gt[1] ) || std::isnan( gt[2] )
       || std::isnan( gt[3] ) || std::isnan( gt[4] ) || std::isnan( gt[5] ) )
  {
    return false;
  }
  // A sensible raster has non-zero pixel size in at least one dimension.
  return std::abs( gt[1] ) > 0.0 || std::abs( gt[5] ) > 0.0;
}

bool allSampledNoData( GDALRasterBandH band, int width, int height )
{
  if ( !band )
    return false;

  int hasNoData = 0;
  const double noDataValue = GDALGetRasterNoDataValue( band, &hasNoData );
  if ( !hasNoData )
    return false; // no declared NoData means we cannot flag the dataset

  constexpr int kMaxSamples = 64;
  const int bufXSize = std::min( kMaxSamples, width );
  const int bufYSize = std::min( kMaxSamples, height );

  // Read a small, regularly down-sampled window.  GDAL handles the decimation.
  std::vector<double> buffer( static_cast<std::size_t>( bufXSize ) * bufYSize );
  if ( GDALRasterIO( band, GF_Read, 0, 0, width, height,
                     buffer.data(), bufXSize, bufYSize, GDT_Float64, 0, 0 ) != CE_None )
  {
    return false;
  }

  for ( double v : buffer )
  {
    if ( std::isnan( noDataValue ) )
    {
      if ( !std::isnan( v ) )
        return false;
    }
    else
    {
      if ( v != noDataValue )
        return false;
    }
  }
  return true;
}

QString wktFromOsr( OGRSpatialReferenceH srs )
{
  if ( !srs )
    return QString();
  char *wkt = nullptr;
  if ( OSRExportToWkt( srs, &wkt ) != OGRERR_NONE )
    return QString();
  QString result = QString::fromUtf8( wkt );
  CPLFree( wkt );
  return result;
}

} // namespace

OutputVerification OutputVerifier::verify( const QString &path, const QString &kindHint ) const
{
  const QString hint = kindHint.isEmpty() ? kindHintFromPath( path ) : kindHint.toLower();

  if ( hint == QStringLiteral( "vector" ) )
    return verifyVector( path );

  // Default to raster (the majority of RS operators) and fall back to vector
  // if the path does not open as a raster.
  OutputVerification raster = verifyRaster( path );
  if ( raster.ok || hint == QStringLiteral( "raster" ) )
    return raster;

  OutputVerification vector = verifyVector( path );
  if ( vector.ok )
    return vector;

  // Neither opened: prefer the raster report but mention both attempts.
  raster.issues.append( QStringLiteral( "Vector probe: %1" ).arg( vector.issues.value( 0, QStringLiteral( "unknown" ) ) ) );
  return raster;
}

OutputVerification OutputVerifier::verifyRaster( const QString &path )
{
  OutputVerification report;
  report.kind = QStringLiteral( "raster" );
  report.ok = false;

  if ( path.isEmpty() )
  {
    addIssue( report, QStringLiteral( "Path is empty" ) );
    return report;
  }

  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( path ) && !QFile::exists( path ) )
  {
    addIssue( report, QStringLiteral( "File does not exist: %1" ).arg( path ) );
    return report;
  }

  ensureGdalInit();
  CPLErrorReset();

  GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                nullptr, nullptr, nullptr );
  if ( !ds )
  {
    const char *msg = CPLGetLastErrorMsg();
    addIssue( report, QStringLiteral( "Cannot open raster: %1" ).arg( msg && msg[0] ? QString::fromUtf8( msg ) : path ) );
    CPLErrorReset();
    return report;
  }

  const int width = GDALGetRasterXSize( ds );
  const int height = GDALGetRasterYSize( ds );
  const int bandCount = GDALGetRasterCount( ds );

  report.summary["width"] = width;
  report.summary["height"] = height;
  report.summary["bandCount"] = bandCount;

  if ( width <= 0 || height <= 0 )
    addIssue( report, QStringLiteral( "Invalid raster dimensions: %1x%2" ).arg( width ).arg( height ) );

  if ( bandCount <= 0 )
    addIssue( report, QStringLiteral( "Raster has no bands" ) );

  const char *proj = GDALGetProjectionRef( ds );
  const QString crs = proj ? QString::fromUtf8( proj ) : QString();
  report.summary["crs"] = crs.toStdString();
  if ( crs.isEmpty() )
    addIssue( report, QStringLiteral( "Raster CRS is missing" ) );

  double gt[6] = { 0, 0, 0, 0, 0, 0 };
  if ( GDALGetGeoTransform( ds, gt ) != CE_None || !isValidGeoTransform( gt ) )
    addIssue( report, QStringLiteral( "Raster geotransform is invalid" ) );
  else
  {
    Json::Value transform( Json::arrayValue );
    for ( int i = 0; i < 6; ++i )
      transform.append( gt[i] );
    report.summary["geoTransform"] = transform;
  }

  if ( bandCount >= 1 )
  {
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( band && allSampledNoData( band, width, height ) )
      addIssue( report, QStringLiteral( "Band 1 sampled pixels are all NoData" ) );
  }

  GDALClose( ds );
  CPLErrorReset();

  if ( report.issues.isEmpty() )
    report.ok = true;

  return report;
}

OutputVerification OutputVerifier::verifyVector( const QString &path )
{
  OutputVerification report;
  report.kind = QStringLiteral( "vector" );
  report.ok = false;

  if ( path.isEmpty() )
  {
    addIssue( report, QStringLiteral( "Path is empty" ) );
    return report;
  }

  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( path ) && !QFile::exists( path ) )
  {
    addIssue( report, QStringLiteral( "File does not exist: %1" ).arg( path ) );
    return report;
  }

  ensureGdalInit();
  CPLErrorReset();

  GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(),
                                GDAL_OF_VECTOR | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR,
                                nullptr, nullptr, nullptr );
  if ( !ds )
  {
    const char *msg = CPLGetLastErrorMsg();
    addIssue( report, QStringLiteral( "Cannot open vector: %1" ).arg( msg && msg[0] ? QString::fromUtf8( msg ) : path ) );
    CPLErrorReset();
    return report;
  }

  const int layerCount = GDALDatasetGetLayerCount( ds );
  report.summary["layerCount"] = layerCount;
  if ( layerCount <= 0 )
  {
    addIssue( report, QStringLiteral( "Vector dataset has no layers" ) );
    GDALClose( ds );
    CPLErrorReset();
    return report;
  }

  OGRLayerH layer = GDALDatasetGetLayer( ds, 0 );
  if ( !layer )
  {
    addIssue( report, QStringLiteral( "Cannot access first vector layer" ) );
    GDALClose( ds );
    CPLErrorReset();
    return report;
  }

  // force=0 (#634): force=1 parsed the whole CSV/GeoJSON on the GUI thread
  // just to count features; unknown (-1) is reported as such by the caller.
  const GIntBig featureCount = OGR_L_GetFeatureCount( layer, 0 );
  report.summary["featureCount"] = static_cast<Json::Int64>( featureCount );
  if ( featureCount == 0 )
    addWarning( report, QStringLiteral( "First layer contains no features" ) );

  const OGRwkbGeometryType geomType = OGR_L_GetGeomType( layer );
  report.summary["geometryType"] = OGRGeometryTypeToName( geomType );

  OGRSpatialReferenceH srs = OGR_L_GetSpatialRef( layer );
  const QString crs = wktFromOsr( srs );
  report.summary["crs"] = crs.toStdString();
  if ( crs.isEmpty() )
    addIssue( report, QStringLiteral( "Vector layer CRS is missing" ) );

  OGREnvelope extent;
  // bForce=1: GeoJSON and other drivers do not precompute extents; without a
  // full scan GetExtent fails (GDAL 3.8 CI) and healthy vectors are rejected.
  const OGRErr extentErr = OGR_L_GetExtent( layer, &extent, 1 );
  if ( featureCount > 0 )
  {
    if ( extentErr != OGRERR_NONE
         || std::isnan( extent.MinX ) || std::isnan( extent.MinY )
         || std::isnan( extent.MaxX ) || std::isnan( extent.MaxY ) )
    {
      addIssue( report, QStringLiteral( "Vector layer extent is invalid" ) );
    }
    else if ( extent.MinX >= extent.MaxX || extent.MinY >= extent.MaxY )
    {
      addIssue( report, QStringLiteral( "Vector layer extent is empty" ) );
    }
    else
    {
      Json::Value ext( Json::arrayValue );
      ext.append( extent.MinX );
      ext.append( extent.MinY );
      ext.append( extent.MaxX );
      ext.append( extent.MaxY );
      report.summary["extent"] = ext;
    }
  }

  GDALClose( ds );
  CPLErrorReset();

  if ( report.issues.isEmpty() )
    report.ok = true;

  return report;
}

QString OutputVerifier::kindHintFromPath( const QString &path )
{
  const QString suffix = QFileInfo( path ).suffix().toLower();
  if ( suffix == QStringLiteral( "shp" ) || suffix == QStringLiteral( "geojson" )
       || suffix == QStringLiteral( "gpkg" ) || suffix == QStringLiteral( "kml" )
       || suffix == QStringLiteral( "csv" ) || suffix == QStringLiteral( "tsv" )
       || suffix == QStringLiteral( "json" ) || suffix == QStringLiteral( "xml" ) )
  {
    return QStringLiteral( "vector" );
  }
  return QStringLiteral( "raster" );
}

// ---------------------------------------------------------------------------
// Teaching grade mode (D4 / ADR 0146)
// ---------------------------------------------------------------------------

namespace lab_grading
{

constexpr const char *kGradeSchemaName = "sicnu.lab.grade/1";
constexpr const char *kRulesSchemaVersion = "sicnu.lab.rules/1";

static const char *const kKnownKinds[] =
{
  "range", "mean_sigma", "gain_invariance", "nodata_ratio", "histogram_shape",
  "classification_kappa", "confusion_marginals", "change_area_interval", "crs_grid",
};

struct LabAssertion
{
  QString id;
  QString kind;
  QString severity = QStringLiteral( "normal" );
  double weight = 0.0;
  Json::Value params;
  QString derivation;
};

struct LabRuleSet
{
  QString labId;
  QString title;
  QString path;
  double passingScore = 60.0;
  std::vector<LabAssertion> assertions;
};

/// Rounds to 12 significant digits so serialized evidence is stable across
/// runs on the same platform (autonomy default 6: byte-identical reports).
double round12( double v )
{
  if ( !std::isfinite( v ) )
    return v;
  const QString text = QString::number( v, 'g', 12 );
  return text.toDouble();
}

Json::Value roundJson( const Json::Value &value )
{
  if ( value.isDouble() || value.isInt() || value.isInt64() || value.isUInt() || value.isUInt64() )
    return Json::Value( round12( value.asDouble() ) );
  if ( value.isArray() )
  {
    Json::Value out( Json::arrayValue );
    for ( const auto &item : value )
      out.append( roundJson( item ) );
    return out;
  }
  if ( value.isObject() )
  {
    Json::Value out( Json::objectValue );
    for ( const auto &key : value.getMemberNames() )
      out[key] = roundJson( value[key] );
    return out;
  }
  return value;
}

bool isKnownKind( const QString &kind )
{
  for ( const char *k : kKnownKinds )
  {
    if ( kind == QLatin1String( k ) )
      return true;
  }
  return false;
}

// --- typed params access (accumulates usage errors) ------------------------

class ParamReader
{
  public:
    explicit ParamReader( const QString &assertionId )
      : mId( assertionId ) {}

    QString error() const { return mErrors.join( QStringLiteral( "; " ) ); }
    bool ok() const { return mErrors.isEmpty(); }

    Json::Value params( const LabAssertion &a ) const { return a.params; }

    bool has( const Json::Value &params, const char *key ) const
    {
      return params.isMember( key ) && !params[key].isNull();
    }

    double number( const Json::Value &params, const char *key, bool *found = nullptr )
    {
      const bool present = has( params, key );
      if ( found )
        *found = present;
      if ( !present )
        return 0.0;
      if ( !params[key].isConvertibleTo( Json::realValue ) )
      {
        mErrors << QStringLiteral( "%1.%2 must be a number" ).arg( mId, key );
        return 0.0;
      }
      return params[key].asDouble();
    }

    int integer( const Json::Value &params, const char *key, bool *found = nullptr )
    {
      const bool present = has( params, key );
      if ( found )
        *found = present;
      if ( !present )
        return 0;
      if ( !params[key].isConvertibleTo( Json::intValue ) )
      {
        mErrors << QStringLiteral( "%1.%2 must be an integer" ).arg( mId, key );
        return 0;
      }
      return params[key].asInt();
    }

    QString text( const Json::Value &params, const char *key, bool *found = nullptr )
    {
      const bool present = has( params, key );
      if ( found )
        *found = present;
      if ( !present )
        return QString();
      if ( !params[key].isString() )
      {
        mErrors << QStringLiteral( "%1.%2 must be a string" ).arg( mId, key );
        return QString();
      }
      return QString::fromStdString( params[key].asString() );
    }

    std::vector<int> intArray( const Json::Value &params, const char *key, bool *found = nullptr )
    {
      const bool present = has( params, key );
      if ( found )
        *found = present;
      if ( !present )
        return {};
      if ( !params[key].isArray() || params[key].empty() )
      {
        mErrors << QStringLiteral( "%1.%2 must be a non-empty array" ).arg( mId, key );
        return {};
      }
      std::vector<int> out;
      for ( const auto &item : params[key] )
      {
        if ( !item.isConvertibleTo( Json::intValue ) )
        {
          mErrors << QStringLiteral( "%1.%2 entries must be integers" ).arg( mId, key );
          return {};
        }
        out.push_back( item.asInt() );
      }
      return out;
    }

    void fail( const QString &message ) { mErrors << QStringLiteral( "%1: %2" ).arg( mId, message ); }

  private:
    QString mId;
    QStringList mErrors;
};

// --- rules parsing / validation (violations are USAGE errors) --------------

bool parseRulesImpl( const QString &path, LabRuleSet *rules, QString *error );

bool parseRules( const QString &path, LabRuleSet *rules, QString *error )
{
    // Malformed member types make jsoncpp's accessors throw (Json::Exception);
    // a rules file must never crash the grader — that is a usage error.
    try
    {
        return parseRulesImpl( path, rules, error );
    }
    catch ( const Json::Exception &e )
    {
        *error = QStringLiteral( "%1: malformed rules document: %2" )
                   .arg( path, QString::fromUtf8( e.what() ) );
        return false;
    }
}

bool parseRulesImpl( const QString &path, LabRuleSet *rules, QString *error )
{
  QFile file( path );
  if ( !file.exists() )
  {
    *error = QStringLiteral( "Rules file does not exist: %1" ).arg( path );
    return false;
  }
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    *error = QStringLiteral( "Cannot read rules file: %1" ).arg( path );
    return false;
  }
  Json::Value root;
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  std::string parseErrors;
  // Parse from memory: Json::parseFromStream wants a std::istream and a QFile
  // is not one (this TU never compiled against real jsoncpp before this fix).
  const QByteArray rulesBytes = file.readAll();
  const char *begin = rulesBytes.constData();
  const char *end = begin + rulesBytes.size();
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  if ( !reader->parse( begin, end, &root, &parseErrors ) )
  {
    *error = QStringLiteral( "Rules file is not valid JSON: %1 (%2)" )
               .arg( path, QString::fromStdString( parseErrors ) );
    return false;
  }

  rules->path = QFileInfo( path ).absoluteFilePath();

  const std::string schemaVersion = root.get( "schema_version", Json::Value() ).asString();
  if ( schemaVersion != kRulesSchemaVersion )
  {
    *error = QStringLiteral( "%1: schema_version must be \"%2\", got \"%3\"" )
               .arg( path, QLatin1String( kRulesSchemaVersion ), QString::fromStdString( schemaVersion ) );
    return false;
  }

  rules->labId = QString::fromStdString( root.get( "lab_id", Json::Value() ).asString() );
  if ( rules->labId.isEmpty() )
  {
    *error = QStringLiteral( "%1: lab_id must not be empty" ).arg( path );
    return false;
  }
  const QString stem = QFileInfo( path ).completeBaseName();
  if ( stem != rules->labId + QStringLiteral( ".rules" ) )
  {
    *error = QStringLiteral( "%1: file name stem must be <lab_id>.rules (lab_id=\"%2\")" )
               .arg( path, rules->labId );
    return false;
  }

  rules->title = QString::fromStdString( root.get( "title", Json::Value() ).asString() );
  if ( rules->title.isEmpty() )
  {
    *error = QStringLiteral( "%1: title must not be empty" ).arg( path );
    return false;
  }

  const Json::Value artifact = root.get( "artifact", Json::Value() );
  if ( !artifact.isObject() || artifact.get( "kind", Json::Value() ).asString() != "raster" )
  {
    *error = QStringLiteral( "%1: artifact.kind must be \"raster\"" ).arg( path );
    return false;
  }

  if ( root.isMember( "passing_score" ) )
  {
    if ( !root["passing_score"].isNumeric() )
    {
      *error = QStringLiteral( "%1: passing_score must be a number" ).arg( path );
      return false;
    }
    rules->passingScore = root["passing_score"].asDouble();
    if ( rules->passingScore < 0.0 || rules->passingScore > 100.0 )
    {
      *error = QStringLiteral( "%1: passing_score must lie in [0,100]" ).arg( path );
      return false;
    }
  }

  const Json::Value assertions = root.get( "assertions", Json::Value() );
  if ( !assertions.isArray() || assertions.empty() )
  {
    *error = QStringLiteral( "%1: assertions must be a non-empty array" ).arg( path );
    return false;
  }

  double weightSum = 0.0;
  QSet<QString> seenIds;
  for ( const auto &item : assertions )
  {
    if ( !item.isObject() )
    {
      *error = QStringLiteral( "%1: every assertion must be an object" ).arg( path );
      return false;
    }
    LabAssertion assertion;
    assertion.id = QString::fromStdString( item.get( "id", Json::Value() ).asString() );
    assertion.kind = QString::fromStdString( item.get( "kind", Json::Value() ).asString() );
    assertion.severity = QString::fromStdString( item.get( "severity", "normal" ).asString() );
    assertion.derivation = QString::fromStdString( item.get( "derivation", Json::Value() ).asString() );
    assertion.params = item.get( "params", Json::Value() );

    if ( assertion.id.isEmpty() )
    {
      *error = QStringLiteral( "%1: assertion id must not be empty" ).arg( path );
      return false;
    }
    if ( seenIds.contains( assertion.id ) )
    {
      *error = QStringLiteral( "%1: duplicate assertion id \"%2\"" ).arg( path, assertion.id );
      return false;
    }
    seenIds.insert( assertion.id );

    if ( !isKnownKind( assertion.kind ) )
    {
      *error = QStringLiteral( "%1: assertion \"%2\" has unknown kind \"%3\"" )
                 .arg( path, assertion.id, assertion.kind );
      return false;
    }
    if ( assertion.severity != QStringLiteral( "normal" )
         && assertion.severity != QStringLiteral( "blocking" ) )
    {
      *error = QStringLiteral( "%1: assertion \"%2\" severity must be normal|blocking" )
                 .arg( path, assertion.id );
      return false;
    }
    if ( !item.isMember( "weight" ) || !item["weight"].isNumeric() || item["weight"].asDouble() <= 0.0
         || item["weight"].asDouble() > 100.0 )
    {
      *error = QStringLiteral( "%1: assertion \"%2\" weight must lie in (0,100]" )
                 .arg( path, assertion.id );
      return false;
    }
    assertion.weight = item["weight"].asDouble();
    weightSum += assertion.weight;

    if ( !assertion.params.isObject() )
    {
      *error = QStringLiteral( "%1: assertion \"%2\" params must be an object" )
                 .arg( path, assertion.id );
      return false;
    }
    rules->assertions.push_back( assertion );
  }

  // Weights are declared to sum to exactly 100 — explicitness over silent
  // normalization (autonomy default 1).
  if ( std::abs( weightSum - 100.0 ) > 1e-9 )
  {
    *error = QStringLiteral( "%1: assertion weights must sum to 100, got %2" )
               .arg( path ).arg( weightSum );
    return false;
  }

  // Per-kind params validation.
  for ( const LabAssertion &a : rules->assertions )
  {
    ParamReader p( a.id );
    const Json::Value &params = a.params;
    bool hasBand = false, hasIndex = false;
    p.integer( params, "band", &hasBand );
    p.text( params, "index", &hasIndex );
    std::vector<int> bands;
    bands = p.intArray( params, "bands" );

    if ( a.kind == QLatin1String( "range" ) || a.kind == QLatin1String( "mean_sigma" ) )
    {
      if ( hasBand == hasIndex )
        p.fail( QStringLiteral( "exactly one of band | index(+bands) is required" ) );
      if ( hasIndex )
      {
        if ( p.text( params, "index" ) != QLatin1String( "ndvi" ) )
          p.fail( "index vocabulary: only \"ndvi\" is defined" );
        if ( bands.size() != 2 )
          p.fail( "index requires bands:[nirBand, redBand]" );
        if ( p.has( params, "bins" ) )
          p.fail( "index mode does not support bins (use a band + histogram_shape)" );
      }
      double minValue = 0.0, maxValue = 0.0;
      if ( a.kind == QLatin1String( "range" ) )
      {
        minValue = p.number( params, "min" );
        maxValue = p.number( params, "max" );
        if ( minValue >= maxValue )
          p.fail( "range min must be < max" );
        const double ratio = p.number( params, "max_violation_ratio" ); // default 0
        if ( ratio < 0.0 || ratio > 1.0 )
          p.fail( "max_violation_ratio must lie in [0,1]" );
      }
      else
      {
        bool hasMean = false, hasSigmaMin = false, hasSigmaMax = false;
        const double mean = p.number( params, "mean", &hasMean );
        const double meanTol = p.number( params, "mean_tolerance", &hasMean );
        p.number( params, "sigma_min", &hasSigmaMin );
        p.number( params, "sigma_max", &hasSigmaMax );
        const double sigmaTol = p.number( params, "sigma_tolerance" );
        if ( !hasMean && !hasSigmaMin && !hasSigmaMax )
          p.fail( "at least one of mean/sigma_min/sigma_max is required" );
        if ( hasMean && meanTol <= 0.0 )
          p.fail( "mean_tolerance must be > 0 when mean is declared" );
        if ( hasSigmaMin && hasSigmaMax && hasSigmaMin > hasSigmaMax )
          p.fail( "sigma_min must be <= sigma_max" );
        if ( sigmaTol < 0.0 )
          p.fail( "sigma_tolerance must be >= 0" );
        Q_UNUSED( mean );
      }
    }
    else if ( a.kind == QLatin1String( "gain_invariance" ) )
    {
      if ( bands.size() != 2 )
        p.fail( "gain_invariance requires bands:[bandA, bandB]" );
      const double tol = p.number( params, "tolerance" );
      if ( tol <= 0.0 )
        p.fail( "tolerance must be > 0" );
      const double ratio = p.number( params, "max_violation_ratio" );
      if ( ratio < 0.0 || ratio > 1.0 )
        p.fail( "max_violation_ratio must lie in [0,1]" );
      bool hasMean = false;
      const double meanTol = p.number( params, "mean_tolerance", &hasMean );
      if ( p.has( params, "expected_mean" ) && !p.has( params, "mean_tolerance" ) )
        p.fail( "expected_mean requires mean_tolerance" );
      if ( p.has( params, "mean_tolerance" ) && !p.has( params, "expected_mean" ) )
        p.fail( "mean_tolerance requires expected_mean" );
      if ( hasMean && meanTol <= 0.0 )
        p.fail( "mean_tolerance must be > 0" );
    }
    else if ( a.kind == QLatin1String( "nodata_ratio" ) )
    {
      const double minRatio = p.number( params, "min_ratio" );
      const double maxRatio = p.number( params, "max_ratio" );
      if ( minRatio < 0.0 || minRatio > 1.0 || maxRatio < 0.0 || maxRatio > 1.0 )
        p.fail( "ratios must lie in [0,1]" );
      if ( minRatio > maxRatio )
        p.fail( "min_ratio must be <= max_ratio" );
    }
    else if ( a.kind == QLatin1String( "histogram_shape" ) )
    {
      const int bins = p.integer( params, "bins" );
      if ( bins < 2 || bins > 4096 )
        p.fail( "bins must lie in [2,4096]" );
      if ( p.number( params, "min" ) >= p.number( params, "max" ) )
        p.fail( "histogram min must be < max" );
      const QString shape = p.text( params, "shape" );
      if ( shape != QLatin1String( "bimodal" ) && shape != QLatin1String( "monotone_increasing" )
           && shape != QLatin1String( "monotone_decreasing" ) )
        p.fail( "shape must be bimodal|monotone_increasing|monotone_decreasing" );
      const double fraction = p.number( params, "min_mode_fraction" ); // default 0.05
      if ( fraction < 0.0 || fraction > 1.0 )
        p.fail( "min_mode_fraction must lie in [0,1]" );
      if ( p.number( params, "min_separation" ) < 0.0 )
        p.fail( "min_separation must be >= 0" );
    }
    else if ( a.kind == QLatin1String( "classification_kappa" )
              || a.kind == QLatin1String( "confusion_marginals" ) )
    {
      if ( !p.has( params, "band" ) )
        p.fail( "band is required" );
      const Json::Value truth = params.get( "truth", Json::Value() );
      const bool hasPath = truth.isObject() && truth.isMember( "path" );
      const bool hasInline = truth.isObject() && truth.isMember( "inline" );
      if ( !hasPath && !hasInline )
        p.fail( "truth must be {path} or {inline}" );
      if ( hasInline && ( !truth["inline"].isArray() || truth["inline"].empty() ) )
        p.fail( "truth.inline must be a non-empty 2D array" );
      const std::vector<int> labels = p.intArray( params, "labels" );
      if ( labels.empty() )
        p.fail( "labels must be a non-empty integer array" );
      if ( a.kind == QLatin1String( "classification_kappa" ) )
      {
        bool hasKappaMin = false;
        const double kappaMin = p.number( params, "kappa_min", &hasKappaMin );
        if ( !hasKappaMin || kappaMin < -1.0 || kappaMin > 1.0 )
          p.fail( "kappa_min must lie in [-1,1]" );
        const double oaMin = p.number( params, "oa_min" );
        if ( p.has( params, "oa_min" ) && ( oaMin < 0.0 || oaMin > 1.0 ) )
          p.fail( "oa_min must lie in [0,1]" );
      }
      else
      {
        bool hasTolerance = false;
        const double tolerance = p.number( params, "tolerance", &hasTolerance );
        if ( !hasTolerance || tolerance <= 0.0 )
          p.fail( "tolerance must be > 0" );
        const Json::Value proportions = params.get( "expected_proportions", Json::Value() );
        if ( !proportions.isObject() || proportions.empty() )
        {
          p.fail( "expected_proportions must be a non-empty object" );
        }
        else
        {
          for ( const int label : labels )
          {
            if ( !proportions.isMember( std::to_string( label ) ) )
            {
              p.fail( QStringLiteral( "expected_proportions is missing label %1" ).arg( label ) );
              break;
            }
          }
          for ( const auto &key : proportions.getMemberNames() )
          {
            const double v = proportions[key].asDouble();
            if ( v < 0.0 || v > 1.0 )
            {
              p.fail( "expected_proportions values must lie in [0,1]" );
              break;
            }
          }
        }
      }
    }
    else if ( a.kind == QLatin1String( "change_area_interval" ) )
    {
      if ( !p.has( params, "band" ) || !p.has( params, "value" ) )
        p.fail( "band and value are required" );
      const qint64 minPx = static_cast<qint64>( p.number( params, "min_px" ) );
      const qint64 maxPx = static_cast<qint64>( p.number( params, "max_px" ) );
      if ( minPx < 0 || maxPx < minPx )
        p.fail( "0 <= min_px <= max_px is required" );
    }
    else if ( a.kind == QLatin1String( "crs_grid" ) )
    {
      if ( p.integer( params, "epsg" ) <= 0 )
        p.fail( "epsg must be a positive authority code" );
      if ( p.integer( params, "width" ) <= 0 || p.integer( params, "height" ) <= 0 )
        p.fail( "width/height must be positive" );
      if ( p.number( params, "pixel_size_x" ) == 0.0 || p.number( params, "pixel_size_y" ) == 0.0 )
        p.fail( "pixel_size_x/y must be non-zero" );
      bool hasBandCount = false;
      if ( p.integer( params, "band_count", &hasBandCount ) < 1 && hasBandCount )
        p.fail( "band_count must be >= 1" );
      if ( p.number( params, "pixel_size_tolerance" ) < 0.0 )
        p.fail( "pixel_size_tolerance must be >= 0" );
    }

    if ( !p.ok() )
    {
      *error = QStringLiteral( "%1: %2" ).arg( path, p.error() );
      return false;
    }
  }

  return true;
}

/// Resolves @a labIdOrRulesPath to a rules file path.
QString resolveRulesPath( const QString &labIdOrRulesPath, const QString &rulesDir )
{
  if ( labIdOrRulesPath.endsWith( QStringLiteral( ".rules.json" ) ) )
    return labIdOrRulesPath;

  QStringList searchDirs;
  if ( !rulesDir.isEmpty() )
    searchDirs << rulesDir;
  const QByteArray envDir = qgetenv( "SICNU_LAB_RULES_DIR" );
  if ( !envDir.isEmpty() )
    searchDirs << QString::fromLocal8Bit( envDir );
#ifdef SICNU_SOURCE_DIR
  searchDirs << QString::fromUtf8( SICNU_SOURCE_DIR ) + QStringLiteral( "/data/labs/grading" );
#endif

  for ( const QString &dir : searchDirs )
  {
    const QString candidate = QDir( dir ).filePath( labIdOrRulesPath + QStringLiteral( ".rules.json" ) );
    if ( QFile::exists( candidate ) )
      return candidate;
  }
  return QString();
}

} // namespace lab_grading

// --- streaming evaluation machinery ----------------------------------------

namespace lab_grading
{
using sicnu::geo::BandInfo;
using sicnu::geo::RasterMetadata;
using sicnu::geo::RasterReader;
using sicnu::geo::RasterWindow;
using sicnu::geo::TilePlan;

/// Per-assertion streaming state.  Only the fields the kernel needs are
/// touched; everything lives in fixed-size accumulators (no pixel buffers
/// outlive a tile).
struct AssertionState
{
    // single-value statistics (band values or derived ndvi-index values)
    qint64 valid = 0;
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    double mean = 0.0;
    double m2 = 0.0;               // Welford; population sigma = sqrt(m2/valid)
    std::vector<qint64> hist;
    qint64 outOfDomain = 0;

    // range
    qint64 rangeViolations = 0;

    // gain invariance
    qint64 pairTotal = 0;
    qint64 pairViolations = 0;
    double pairMaxDelta = 0.0;
    qint64 pairSkipped = 0;        // undefined index (zero denominator)

    // nodata / area
    qint64 totalPixels = 0;
    qint64 nodataPixels = 0;
    qint64 valueCount = 0;

    // classification
    std::unique_ptr<sicnu::experiment::ConfusionMatrix> matrix;
    qint64 truthValid = 0;
    qint64 unlabelled = 0;

    void addValue( double v )
    {
        ++valid;
        minValue = std::min( minValue, v );
        maxValue = std::max( maxValue, v );
        const double delta = v - mean;
        mean += delta / static_cast<double>( valid );
        m2 += delta * ( v - mean );
    }

    void addHist( double v, int bins, double lo, double hi )
    {
        if ( hist.empty() )
            hist.assign( static_cast<std::size_t>( bins ), 0 );
        if ( !std::isfinite( v ) || v < lo || v > hi )
        {
            ++outOfDomain;
            return;
        }
        const double width = ( hi - lo ) / bins;
        int bin = static_cast<int>( ( v - lo ) / width );
        if ( bin >= bins )
            bin = bins - 1;          // right-open bins, last bin closed
        if ( bin < 0 )
            bin = 0;
        ++hist[static_cast<std::size_t>( bin )];
    }
};

/// Resolves which artifact bands an assertion reads (1-based).  Declared
/// out-of-range bands are kept so the kernel reports "artifact lacks band N"
/// instead of silently substituting other bands.
std::vector<int> assertionBands( const LabAssertion &a, bool *needsAllBands )
{
    std::vector<int> bands;
    const Json::Value &params = a.params;
    if ( a.kind == QLatin1String( "nodata_ratio" ) && !params.isMember( "bands" ) )
    {
        *needsAllBands = true;     // default: AND-combine every artifact band
        return bands;
    }
    if ( params.isMember( "bands" ) )
    {
        for ( const auto &item : params["bands"] )
            bands.push_back( item.asInt() );
    }
    else if ( params.isMember( "band" ) )
    {
        bands.push_back( params["band"].asInt() );
    }
    return bands;
}

/// True when the stored value equals the band's declared NoData.
bool isNoData( const BandInfo &band, double v )
{
    if ( band.hasNoData )
    {
        if ( band.noDataIsNaN )
            return std::isnan( v );
        return v == band.noDataValue;
    }
    // Float bands without a declaration: NaN is data per the reader contract,
    // but non-finite values cannot carry a known answer — the grader skips
    // them and reports the count (documented in docs/labs/GRADING.md).
    return false;
}

bool isNonFinite( double v ) { return !std::isfinite( v ); }

struct GradeContext
{
    const RasterMetadata *meta = nullptr;
    std::size_t maxBytes = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    qint64 tiles = 0;
    QString error;                 // artifact-class failure
};

/// Streams one tile walk feeding every content assertion's state.
class ContentWalk
{
  public:
    ContentWalk( const RasterReader &reader, const LabRuleSet &rules, GradeContext &ctx,
                 std::map<QString, AssertionState> &states )
      : mReader( reader )
      , mRules( rules )
      , mCtx( ctx )
      , mStates( states )
    {
        mMeta = &reader.metadata();
        collectNeeds();
    }

    bool run();

  private:
    void collectNeeds();
    bool planTiles( int *tileW, int *tileH ) const;
    void feedTile( const sicnu::geo::TileSlice &slice, const std::vector<double> &buffer );

    const RasterMetadata *mMeta = nullptr;
    const RasterReader &mReader;
    const LabRuleSet &mRules;
    GradeContext &mCtx;
    std::map<QString, AssertionState> &mStates;

    struct Need
    {
        const LabAssertion *assertion = nullptr;
        std::vector<int> bands;        // artifact bands (1-based)
        bool allBands = false;
        std::vector<int> readBands;    // positions resolved against mUnionBands
    };
    std::vector<Need> mNeeds;
    std::vector<int> mUnionBands;
};

void ContentWalk::collectNeeds()
{
    const int bandCount = mMeta->bandCount;
    for ( const LabAssertion &a : mRules.assertions )
    {
        const bool contentKind = a.kind != QLatin1String( "crs_grid" )
                                 && a.kind != QLatin1String( "classification_kappa" )
                                 && a.kind != QLatin1String( "confusion_marginals" );
        if ( !contentKind )
            continue;
        Need need;
        need.assertion = &a;
        need.bands = assertionBands( a, &need.allBands );
        if ( need.allBands )
        {
            for ( int b = 1; b <= bandCount; ++b )
                need.readBands.push_back( b );
        }
        else
        {
            need.readBands = need.bands;
        }
        mNeeds.push_back( need );
        for ( int b : need.readBands )
        {
            if ( std::find( mUnionBands.begin(), mUnionBands.end(), b ) == mUnionBands.end()
                 && b >= 1 && b <= bandCount )
            {
                mUnionBands.push_back( b );
            }
        }
    }
    std::sort( mUnionBands.begin(), mUnionBands.end() );
}

bool ContentWalk::planTiles( int *tileW, int *tileH ) const
{
    if ( mUnionBands.empty() )
        return true;               // metadata-only grading
    const std::size_t bytesPerPixel = mUnionBands.size() * sizeof( double );
    if ( bytesPerPixel == 0 || bytesPerPixel > mCtx.maxBytes )
    {
        mCtx.error = QStringLiteral( "byte budget (%1) is below a single pixel (%2 bytes)"
                                   ).arg( mCtx.maxBytes ).arg( bytesPerPixel );
        return false;
    }
    int w = mMeta->width;
    int h = 1;
    const std::size_t perRow = bytesPerPixel * static_cast<std::size_t>( mMeta->width );
    if ( perRow <= mCtx.maxBytes )
    {
        h = static_cast<int>( mCtx.maxBytes / perRow );
    }
    else
    {
        w = static_cast<int>( mCtx.maxBytes / bytesPerPixel );
        if ( w < 1 )
        {
            mCtx.error = QStringLiteral( "byte budget (%1) cannot hold one pixel row" ).arg( mCtx.maxBytes );
            return false;
        }
    }
    *tileW = std::min( w, mMeta->width );
    *tileH = std::max( 1, std::min( h, mMeta->height ) );
    return true;
}

bool ContentWalk::run()
{
    if ( mUnionBands.empty() )
        return true;
    int tileW = 0, tileH = 0;
    if ( !planTiles( &tileW, &tileH ) )
        return false;
    mCtx.tileWidth = tileW;
    mCtx.tileHeight = tileH;

    RasterWindow full;
    full.width = mMeta->width;
    full.height = mMeta->height;
    try
    {
        const TilePlan plan = sicnu::geo::planTileWalk( *mMeta, full, tileW, tileH );
        mCtx.tiles = static_cast<qint64>( plan.tileCount() );

        auto sink = [this]( const sicnu::geo::TileSlice &slice, const std::vector<double> &buffer )
        {
            feedTile( slice, buffer );
        };
        mReader.iterateTiles( plan, mUnionBands, sink, {} );
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        mCtx.error = QString::fromUtf8( e.what() );
        return false;
    }
    return true;
}

void ContentWalk::feedTile( const sicnu::geo::TileSlice &slice, const std::vector<double> &buffer )
{
    const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;

    for ( const Need &need : mNeeds )
    {
        AssertionState &st = mStates[need.assertion->id];
        const Json::Value &params = need.assertion->params;

        // Resolve band positions; out-of-range bands are graded failures.
        bool outOfRange = false;
        std::vector<std::size_t> positions;
        for ( int b : need.readBands )
        {
            const auto it = std::find( mUnionBands.begin(), mUnionBands.end(), b );
            if ( it == mUnionBands.end() || b < 1 || b > mMeta->bandCount )
            {
                outOfRange = true;
                break;
            }
            positions.push_back( static_cast<std::size_t>( it - mUnionBands.begin() ) );
        }

        const bool indexMode = need.assertion->kind == QLatin1String( "mean_sigma" )
                                   && params.isMember( "index" );
        const bool rangeIndexMode = need.assertion->kind == QLatin1String( "range" )
                                        && params.isMember( "index" );

        if ( need.assertion->kind == QLatin1String( "nodata_ratio" ) )
        {
            st.totalPixels += static_cast<qint64>( plane );
            if ( outOfRange )
                continue;
            for ( std::size_t p = 0; p < plane; ++p )
            {
                bool anyNoData = false;
                for ( std::size_t pos = 0; pos < positions.size(); ++pos )
                {
                    const int bandIndex1Based = need.readBands[pos];
                    const BandInfo &band = mMeta->bands.at( static_cast<std::size_t>( bandIndex1Based - 1 ) );
                    const double v = buffer[positions[pos] * plane + p];
                    if ( isNoData( band, v ) || isNonFinite( v ) )
                    {
                        anyNoData = true;
                        break;
                    }
                }
                if ( anyNoData )
                    ++st.nodataPixels;
            }
            continue;
        }

        if ( need.assertion->kind == QLatin1String( "change_area_interval" ) )
        {
            if ( outOfRange )
                continue;
            const std::size_t pos = positions[0];
            const int bandIndex1Based = need.readBands[0];
            const BandInfo &band = mMeta->bands.at( static_cast<std::size_t>( bandIndex1Based - 1 ) );
            const double target = params["value"].asDouble();
            for ( std::size_t p = 0; p < plane; ++p )
            {
                const double v = buffer[pos * plane + p];
                if ( isNoData( band, v ) || isNonFinite( v ) )
                    continue;
                ++st.totalPixels;
                if ( v == target )
                    ++st.valueCount;
            }
            continue;
        }

        if ( need.assertion->kind == QLatin1String( "gain_invariance" ) )
        {
            if ( outOfRange || positions.size() != 2 )
                continue;
            const BandInfo &bandA = mMeta->bands.at( static_cast<std::size_t>( need.readBands[0] - 1 ) );
            const BandInfo &bandB = mMeta->bands.at( static_cast<std::size_t>( need.readBands[1] - 1 ) );
            const double tolerance = params["tolerance"].asDouble();
            const std::size_t posA = positions[0];
            const std::size_t posB = positions[1];
            for ( std::size_t p = 0; p < plane; ++p )
            {
                const double sa = buffer[posA * plane + p];
                const double sb = buffer[posB * plane + p];
                if ( isNoData( bandA, sa ) || isNoData( bandB, sb )
                     || isNonFinite( sa ) || isNonFinite( sb ) )
                {
                    continue;
                }
                const double pa = RasterReader::applyScaleOffset( bandA, sa );
                const double pb = RasterReader::applyScaleOffset( bandB, sb );
                const double denS = sa + sb;
                const double denP = pa + pb;
                if ( std::abs( denS ) < 1e-30 || std::abs( denP ) < 1e-30 )
                {
                    ++st.pairSkipped;
                    continue;
                }
                const double idxS = ( sa - sb ) / denS;
                const double idxP = ( pa - pb ) / denP;
                ++st.pairTotal;
                const double delta = std::abs( idxS - idxP );
                st.pairMaxDelta = std::max( st.pairMaxDelta, delta );
                if ( delta > tolerance )
                    ++st.pairViolations;
                st.addValue( idxS );
            }
            continue;
        }

        // range / mean_sigma / histogram_shape: single-band values or
        // derived ndvi-index values
        const bool valueKind = need.assertion->kind == QLatin1String( "range" )
                               || need.assertion->kind == QLatin1String( "mean_sigma" )
                               || need.assertion->kind == QLatin1String( "histogram_shape" );
        if ( valueKind )
        {
            if ( outOfRange )
                continue;
            if ( indexMode || rangeIndexMode )
            {
                if ( positions.size() != 2 )
                    continue;
                const BandInfo &bandN = mMeta->bands.at( static_cast<std::size_t>( need.readBands[0] - 1 ) );
                const BandInfo &bandR = mMeta->bands.at( static_cast<std::size_t>( need.readBands[1] - 1 ) );
                const std::size_t posN = positions[0];
                const std::size_t posR = positions[1];
                const int bins = params.isMember( "bins" ) ? params["bins"].asInt() : 0;
                const double rangeMin = params.isMember( "min" ) ? params["min"].asDouble() : 0.0;
                const double rangeMax = params.isMember( "max" ) ? params["max"].asDouble() : 0.0;
                for ( std::size_t p = 0; p < plane; ++p )
                {
                    const double nir = buffer[posN * plane + p];
                    const double red = buffer[posR * plane + p];
                    if ( isNoData( bandN, nir ) || isNoData( bandR, red )
                         || isNonFinite( nir ) || isNonFinite( red ) )
                    {
                        continue;
                    }
                    const double den = nir + red;
                    if ( std::abs( den ) < 1e-30 )
                    {
                        ++st.pairSkipped;
                        continue;
                    }
                    const double idx = ( nir - red ) / den;
                    st.addValue( idx );
                    if ( bins > 0 )
                        st.addHist( idx, bins, params["min"].asDouble(), params["max"].asDouble() );
                    if ( rangeIndexMode && ( idx < rangeMin || idx > rangeMax ) )
                        ++st.rangeViolations;
                }
            }
            else
            {
                const std::size_t pos = positions[0];
                const BandInfo &band = mMeta->bands.at( static_cast<std::size_t>( need.readBands[0] - 1 ) );
                const int bins = params.isMember( "bins" ) ? params["bins"].asInt() : 0;
                const double rangeMin = params.isMember( "min" ) ? params["min"].asDouble() : 0.0;
                const double rangeMax = params.isMember( "max" ) ? params["max"].asDouble() : 0.0;
                for ( std::size_t p = 0; p < plane; ++p )
                {
                    const double v = buffer[pos * plane + p];
                    if ( isNoData( band, v ) || isNonFinite( v ) )
                        continue;
                    st.addValue( v );
                    if ( bins > 0 )
                        st.addHist( v, bins, rangeMin, rangeMax );
                    if ( need.assertion->kind == QLatin1String( "range" ) )
                    {
                        if ( v < rangeMin || v > rangeMax )
                            ++st.rangeViolations;
                    }
                }
            }
        }
    }
}

} // namespace lab_grading

// --- assertion finalization + classification walks --------------------------

namespace lab_grading
{

struct FinalOutcome
{
    bool passed = false;
    bool hasDelta = false;
    double delta = 0.0;
    QString message;
    Json::Value observed;
    Json::Value expected;
};

FinalOutcome makeOutcome( bool passed, Json::Value observed, Json::Value expected,
                          const QString &message = QString(), bool hasDelta = false,
                          double delta = 0.0 )
{
    FinalOutcome o;
    o.passed = passed;
    o.observed = std::move( observed );
    o.expected = std::move( expected );
    o.message = message;
    o.hasDelta = hasDelta;
    o.delta = delta;
    return o;
}

/// Shared tile geometry: bytesPerPixel = bands * sizeof(double).
bool planTileGeometry( int width, int height, std::size_t bands, std::size_t maxBytes,
                       int *tileW, int *tileH, QString *error )
{
    const std::size_t bytesPerPixel = bands * sizeof( double );
    if ( bytesPerPixel == 0 || bytesPerPixel > maxBytes )
    {
        *error = QStringLiteral( "byte budget (%1) is below a single pixel (%2 bytes)" )
                   .arg( maxBytes ).arg( bytesPerPixel );
        return false;
    }
    int w = width;
    int h = 1;
    const std::size_t perRow = bytesPerPixel * static_cast<std::size_t>( width );
    if ( perRow <= maxBytes )
    {
        h = static_cast<int>( maxBytes / perRow );
    }
    else
    {
        w = static_cast<int>( maxBytes / bytesPerPixel );
        if ( w < 1 )
        {
            *error = QStringLiteral( "byte budget (%1) cannot hold one pixel row" ).arg( maxBytes );
            return false;
        }
    }
    *tileW = std::min( w, width );
    *tileH = std::max( 1, std::min( h, height ) );
    return true;
}

Json::Value bandAvailabilityObserved( int band, const RasterMetadata &meta )
{
    Json::Value o;
    o["band"] = band;
    o["artifact_band_count"] = meta.bandCount;
    return o;
}

/// First declared band that the artifact does not have (0 when none).
int firstMissingBand( const Json::Value &params, const RasterMetadata &meta )
{
    if ( params.isMember( "bands" ) )
    {
        for ( const auto &item : params["bands"] )
        {
            if ( item.asInt() > meta.bandCount )
                return item.asInt();
        }
    }
    if ( params.isMember( "band" ) && params["band"].asInt() > meta.bandCount )
        return params["band"].asInt();
    return 0;
}

FinalOutcome finalizeAssertion( const LabAssertion &a, const AssertionState &st,
                                const RasterMetadata &meta )
{
    const Json::Value &params = a.params;

    if ( a.kind == QLatin1String( "crs_grid" ) )
    {
        const int epsg = params["epsg"].asInt();
        const QString expectedAuth = QStringLiteral( "EPSG:%1" ).arg( epsg );
        Json::Value observed;
        observed["crs_authid"] = meta.crs.authid;
        observed["epsg"] = meta.crs.authid;
        observed["width"] = meta.width;
        observed["height"] = meta.height;
        observed["band_count"] = meta.bandCount;
        observed["pixel_size_x"] = meta.resolutionX;
        observed["pixel_size_y"] = meta.resolutionY;
        Json::Value expected;
        expected["crs_authid"] = expectedAuth.toStdString();
        expected["width"] = params["width"].asInt();
        expected["height"] = params["height"].asInt();
        if ( params.isMember( "band_count" ) )
            expected["band_count"] = params["band_count"].asInt();
        expected["pixel_size_x"] = params["pixel_size_x"].asDouble();
        expected["pixel_size_y"] = params["pixel_size_y"].asDouble();
        expected["pixel_size_tolerance"] = params.isMember( "pixel_size_tolerance" )
                                             ? params["pixel_size_tolerance"].asDouble() : 1e-9;

        QString message;
        if ( QString::fromStdString( meta.crs.authid ).compare( expectedAuth, Qt::CaseInsensitive ) != 0 )
            message = QStringLiteral( "CRS %1 does not match expected %2" )
                        .arg( QString::fromStdString( meta.crs.authid ), expectedAuth );
        else if ( meta.width != params["width"].asInt() || meta.height != params["height"].asInt() )
            message = QStringLiteral( "Grid %1x%2 does not match expected %3x%4" )
                        .arg( meta.width ).arg( meta.height )
                        .arg( params["width"].asInt() ).arg( params["height"].asInt() );
        else if ( params.isMember( "band_count" ) && meta.bandCount != params["band_count"].asInt() )
            message = QStringLiteral( "Band count %1 does not match expected %2" )
                        .arg( meta.bandCount ).arg( params["band_count"].asInt() );
        else if ( meta.resolutionX == 0.0 || meta.resolutionY == 0.0 )
            message = QStringLiteral( "Artifact declares no pixel size" );
        else
        {
            const double tol = expected["pixel_size_tolerance"].asDouble();
            if ( std::abs( meta.resolutionX - params["pixel_size_x"].asDouble() ) > tol
                 || std::abs( meta.resolutionY - params["pixel_size_y"].asDouble() ) > tol )
                message = QStringLiteral( "Pixel size %1x%2 deviates beyond tolerance %3" )
                            .arg( meta.resolutionX ).arg( meta.resolutionY ).arg( tol );
        }
        return makeOutcome( message.isEmpty(), observed, expected, message );
    }

    if ( a.kind == QLatin1String( "range" ) || a.kind == QLatin1String( "mean_sigma" ) )
    {
        const bool indexMode = params.isMember( "index" );
        const int band = indexMode ? -1 : params["band"].asInt();
        if ( !indexMode && band > meta.bandCount )
            return makeOutcome( false, bandAvailabilityObserved( band, meta ),
                                Json::Value(), QStringLiteral( "Artifact lacks band %1" ).arg( band ) );
        if ( st.valid == 0 )
        {
            // Structured vacuous evidence (an evidence-less deduction is a P0)
            Json::Value observed;
            observed["valid"] = 0;
            observed["total_pixels"] = static_cast<Json::Int64>( st.totalPixels );
            Json::Value expected;
            if ( a.kind == QLatin1String( "range" ) )
            {
                expected["min"] = params["min"].asDouble();
                expected["max"] = params["max"].asDouble();
            }
            else if ( params.isMember( "mean" ) )
            {
                expected["mean"] = params["mean"].asDouble();
            }
            return makeOutcome( false, observed, expected,
                                QStringLiteral( "No valid pixels to evaluate" ) );
        }

        if ( a.kind == QLatin1String( "range" ) )
        {
            Json::Value observed;
            observed["min"] = st.minValue;
            observed["max"] = st.maxValue;
            observed["valid"] = static_cast<Json::Int64>( st.valid );
            observed["violations"] = static_cast<Json::Int64>( st.rangeViolations );
            observed["violation_ratio"] =
              static_cast<double>( st.rangeViolations ) / static_cast<double>( st.valid );
            Json::Value expected;
            expected["min"] = params["min"].asDouble();
            expected["max"] = params["max"].asDouble();
            expected["max_violation_ratio"] = params.isMember( "max_violation_ratio" )
                                                ? params["max_violation_ratio"].asDouble() : 0.0;
            const bool pass = observed["violation_ratio"].asDouble()
                              <= expected["max_violation_ratio"].asDouble();
            QString message;
            if ( !pass )
                message = QStringLiteral( "%1 of %2 valid pixels fall outside [%3, %4]" )
                            .arg( st.rangeViolations ).arg( st.valid )
                            .arg( params["min"].asDouble() ).arg( params["max"].asDouble() );
            return makeOutcome( pass, observed, expected, message );
        }

        // mean_sigma
        const double sigma = std::sqrt( std::max( 0.0, st.m2 ) / static_cast<double>( st.valid ) );
        Json::Value observed;
        observed["mean"] = st.mean;
        observed["sigma"] = sigma;
        observed["valid"] = static_cast<Json::Int64>( st.valid );
        observed["min"] = st.minValue;
        observed["max"] = st.maxValue;
        Json::Value expected;
        const bool hasMean = params.isMember( "mean" );
        if ( hasMean )
        {
            expected["mean"] = params["mean"].asDouble();
            expected["mean_tolerance"] = params["mean_tolerance"].asDouble();
        }
        if ( params.isMember( "sigma_min" ) )
            expected["sigma_min"] = params["sigma_min"].asDouble();
        if ( params.isMember( "sigma_max" ) )
            expected["sigma_max"] = params["sigma_max"].asDouble();
        const double sigmaTol = params.isMember( "sigma_tolerance" )
                                  ? params["sigma_tolerance"].asDouble() : 0.0;

        QString message;
        bool pass = true;
        if ( hasMean && std::abs( st.mean - params["mean"].asDouble() )
             > params["mean_tolerance"].asDouble() )
        {
            pass = false;
            message = QStringLiteral( "Mean %1 deviates from expected %2 beyond %3" )
                        .arg( st.mean ).arg( params["mean"].asDouble() )
                        .arg( params["mean_tolerance"].asDouble() );
        }
        if ( pass && params.isMember( "sigma_min" )
             && sigma < params["sigma_min"].asDouble() - sigmaTol )
        {
            pass = false;
            message = QStringLiteral( "Sigma %1 below declared floor %2" )
                        .arg( sigma ).arg( params["sigma_min"].asDouble() );
        }
        if ( pass && params.isMember( "sigma_max" )
             && sigma > params["sigma_max"].asDouble() + sigmaTol )
        {
            pass = false;
            message = QStringLiteral( "Sigma %1 above declared ceiling %2" )
                        .arg( sigma ).arg( params["sigma_max"].asDouble() );
        }
        const double delta = hasMean ? st.mean - params["mean"].asDouble() : 0.0;
        return makeOutcome( pass, observed, expected, message, hasMean, delta );
    }

    if ( a.kind == QLatin1String( "gain_invariance" ) )
    {
        for ( int b : { params["bands"][0].asInt(), params["bands"][1].asInt() } )
        {
            if ( b > meta.bandCount )
                return makeOutcome( false, bandAvailabilityObserved( b, meta ), Json::Value(),
                                    QStringLiteral( "Artifact lacks band %1" ).arg( b ) );
        }
        if ( st.pairTotal == 0 )
        {
            Json::Value observed;
            observed["pair_total"] = 0;
            observed["pair_violations"] = 0;
            observed["undefined_index_pixels"] = static_cast<Json::Int64>( st.pairSkipped );
            Json::Value expected;
            expected["tolerance"] = params["tolerance"].asDouble();
            return makeOutcome( false, observed, expected,
                                QStringLiteral( "No comparable pixel pairs" ) );
        }
        const double ratio = static_cast<double>( st.pairViolations )
                             / static_cast<double>( st.pairTotal );
        Json::Value observed;
        observed["violations"] = static_cast<Json::Int64>( st.pairViolations );
        observed["pair_total"] = static_cast<Json::Int64>( st.pairTotal );
        observed["violation_ratio"] = ratio;
        observed["max_abs_delta"] = st.pairMaxDelta;
        observed["index_mean"] = st.mean;
        observed["index_valid"] = static_cast<Json::Int64>( st.valid );
        observed["undefined_index_pixels"] = static_cast<Json::Int64>( st.pairSkipped );
        Json::Value declaredScale( Json::arrayValue );
        Json::Value declaredOffset( Json::arrayValue );
        for ( int b : { params["bands"][0].asInt(), params["bands"][1].asInt() } )
        {
            const BandInfo &band = meta.bands.at( static_cast<std::size_t>( b - 1 ) );
            declaredScale.append( band.scale );
            declaredOffset.append( band.offset );
        }
        observed["declared_scale"] = declaredScale;
        observed["declared_offset"] = declaredOffset;

        Json::Value expected;
        expected["tolerance"] = params["tolerance"].asDouble();
        expected["max_violation_ratio"] = params.isMember( "max_violation_ratio" )
                                            ? params["max_violation_ratio"].asDouble() : 0.0;
        const bool hasMean = params.isMember( "expected_mean" );
        if ( hasMean )
        {
            expected["expected_mean"] = params["expected_mean"].asDouble();
            expected["mean_tolerance"] = params["mean_tolerance"].asDouble();
        }

        const bool ratioOk = ratio <= expected["max_violation_ratio"].asDouble();
        bool meanOk = true;
        double meanDelta = 0.0;
        if ( hasMean )
        {
            meanDelta = st.mean - params["expected_mean"].asDouble();
            meanOk = std::abs( meanDelta ) <= params["mean_tolerance"].asDouble();
        }
        QString message;
        if ( !ratioOk )
            message = QStringLiteral( "%1 of %2 pixel pairs violate gain invariance beyond tolerance %3" )
                        .arg( st.pairViolations ).arg( st.pairTotal ).arg( params["tolerance"].asDouble() );
        else if ( !meanOk )
            message = QStringLiteral( "Index mean %1 deviates from expected %2 beyond %3" )
                        .arg( st.mean ).arg( params["expected_mean"].asDouble() )
                        .arg( params["mean_tolerance"].asDouble() );
        return makeOutcome( ratioOk && meanOk, observed, expected, message,
                            hasMean, meanDelta );
    }

    if ( a.kind == QLatin1String( "nodata_ratio" ) )
    {
        const int missing = firstMissingBand( params, meta );
        if ( missing > 0 )
            return makeOutcome( false, bandAvailabilityObserved( missing, meta ), Json::Value(),
                                QStringLiteral( "Artifact lacks band %1" ).arg( missing ) );
        Json::Value observed;
        observed["nodata_pixels"] = static_cast<Json::Int64>( st.nodataPixels );
        observed["total_pixels"] = static_cast<Json::Int64>( st.totalPixels );
        observed["ratio"] = st.totalPixels > 0
                              ? static_cast<double>( st.nodataPixels )
                                  / static_cast<double>( st.totalPixels )
                              : 1.0;
        Json::Value expected;
        expected["min_ratio"] = params["min_ratio"].asDouble();
        expected["max_ratio"] = params["max_ratio"].asDouble();
        const double ratio = observed["ratio"].asDouble();
        QString message;
        double delta = 0.0;
        bool pass = st.totalPixels > 0;
        if ( pass && ratio < params["min_ratio"].asDouble() )
        {
            pass = false;
            delta = ratio - params["min_ratio"].asDouble();
            message = QStringLiteral( "NoData ratio %1 below floor %2 (forgot to mask?)" )
                        .arg( ratio ).arg( params["min_ratio"].asDouble() );
        }
        else if ( pass && ratio > params["max_ratio"].asDouble() )
        {
            pass = false;
            delta = ratio - params["max_ratio"].asDouble();
            message = QStringLiteral( "NoData ratio %1 above ceiling %2 (masked usable data?)" )
                        .arg( ratio ).arg( params["max_ratio"].asDouble() );
        }
        return makeOutcome( pass, observed, expected, message, !pass, delta );
    }

    if ( a.kind == QLatin1String( "histogram_shape" ) )
    {
        const int missing = firstMissingBand( params, meta );
        if ( missing > 0 )
            return makeOutcome( false, bandAvailabilityObserved( missing, meta ), Json::Value(),
                                QStringLiteral( "Artifact lacks band %1" ).arg( missing ) );
        if ( st.valid == 0 )
        {
            Json::Value observed;
            observed["valid"] = 0;
            observed["bins"] = params["bins"].asInt();
            Json::Value expected;
            expected["shape"] = params["shape"].asString();
            return makeOutcome( false, observed, expected,
                                QStringLiteral( "No valid pixels to histogram" ) );
        }
        const int bins = params["bins"].asInt();
        const double lo = params["min"].asDouble();
        const double hi = params["max"].asDouble();
        const double binWidth = ( hi - lo ) / bins;
        const double minFraction = params.isMember( "min_mode_fraction" )
                                     ? params["min_mode_fraction"].asDouble() : 0.05;
        const double minSeparation = params.isMember( "min_separation" )
                                       ? params["min_separation"].asDouble() : 0.0;
        const double minCount = minFraction * static_cast<double>( st.valid );

        Json::Value observed;
        observed["bins"] = bins;
        observed["valid"] = static_cast<Json::Int64>( st.valid );
        observed["out_of_domain"] = static_cast<Json::Int64>( st.outOfDomain );
        observed["min_mode_count"] = minCount;

        const QString shape = QString::fromStdString( params["shape"].asString() );
        Json::Value expected;
        expected["shape"] = shape.toStdString();
        expected["min"] = lo;
        expected["max"] = hi;
        expected["min_mode_fraction"] = minFraction;

        if ( shape == QLatin1String( "bimodal" ) )
        {
            std::vector<int> modes;
            for ( int i = 0; i < bins; ++i )
            {
                const double count = static_cast<double>( st.hist.at( static_cast<std::size_t>( i ) ) );
                if ( count < minCount )
                    continue;
                const double left = i == 0 ? -1.0
                                           : static_cast<double>( st.hist.at( static_cast<std::size_t>( i - 1 ) ) );
                const double right = i == bins - 1
                                       ? -1.0
                                       : static_cast<double>( st.hist.at( static_cast<std::size_t>( i + 1 ) ) );
                if ( count > left && count >= right )
                    modes.push_back( i );
            }
            Json::Value centers( Json::arrayValue );
            Json::Value counts( Json::arrayValue );
            for ( int m : modes )
            {
                centers.append( lo + ( static_cast<double>( m ) + 0.5 ) * binWidth );
                counts.append( Json::Value( static_cast<Json::Int64>( st.hist.at( static_cast<std::size_t>( m ) ) ) ) );
            }
            observed["mode_centers"] = centers;
            observed["mode_counts"] = counts;
            bool pass = modes.size() >= 2;
            QString message;
            if ( !pass )
            {
                message = QStringLiteral( "Expected at least 2 separated modes, found %1" )
                            .arg( modes.size() );
            }
            else
            {
                for ( std::size_t i = 0; i < modes.size() && pass; ++i )
                {
                    for ( std::size_t j = i + 1; j < modes.size(); ++j )
                    {
                        const double c1 = lo + ( static_cast<double>( modes[i] ) + 0.5 ) * binWidth;
                        const double c2 = lo + ( static_cast<double>( modes[j] ) + 0.5 ) * binWidth;
                        if ( std::abs( c1 - c2 ) < minSeparation )
                        {
                            pass = false;
                            message = QStringLiteral( "Modes %1 and %2 are not separated by %3" )
                                        .arg( c1 ).arg( c2 ).arg( minSeparation );
                            break;
                        }
                    }
                }
            }
            return makeOutcome( pass, observed, expected, message );
        }

        // monotone_increasing / monotone_decreasing over bin counts
        bool monotone = true;
        qint64 violatingSteps = 0;
        for ( int i = 1; i < bins; ++i )
        {
            const double prev = static_cast<double>( st.hist.at( static_cast<std::size_t>( i - 1 ) ) );
            const double cur = static_cast<double>( st.hist.at( static_cast<std::size_t>( i ) ) );
            const bool violated = shape == QLatin1String( "monotone_increasing" ) ? cur < prev
                                                                                  : cur > prev;
            if ( violated )
            {
                monotone = false;
                ++violatingSteps;
            }
        }
        observed["violating_steps"] = static_cast<Json::Int64>( violatingSteps );
        expected["allowed_violating_steps"] = 0;
        QString message;
        if ( !monotone )
            message = QStringLiteral( "Histogram has %1 violating steps for shape %2" )
                        .arg( violatingSteps ).arg( shape );
        return makeOutcome( monotone, observed, expected, message );
    }

    if ( a.kind == QLatin1String( "change_area_interval" ) )
    {
        if ( params["band"].asInt() > meta.bandCount )
            return makeOutcome( false, bandAvailabilityObserved( params["band"].asInt(), meta ),
                                Json::Value(),
                                QStringLiteral( "Artifact lacks band %1" ).arg( params["band"].asInt() ) );
        Json::Value observed;
        observed["value"] = params["value"].asDouble();
        observed["value_count"] = static_cast<Json::Int64>( st.valueCount );
        observed["valid_pixels"] = static_cast<Json::Int64>( st.totalPixels );
        Json::Value expected;
        expected["value"] = params["value"].asDouble();
        expected["min_px"] = static_cast<Json::Int64>( params["min_px"].asDouble() );
        expected["max_px"] = static_cast<Json::Int64>( params["max_px"].asDouble() );
        QString message;
        double delta = 0.0;
        bool pass = st.totalPixels > 0;
        if ( !pass )
        {
            message = QStringLiteral( "No valid pixels to evaluate" );
        }
        else if ( st.valueCount < expected["min_px"].asInt64() )
        {
            pass = false;
            delta = static_cast<double>( st.valueCount - expected["min_px"].asInt64() );
            message = QStringLiteral( "Changed pixels %1 below the known interval [%2, %3]" )
                        .arg( st.valueCount ).arg( expected["min_px"].asInt64() )
                        .arg( expected["max_px"].asInt64() );
        }
        else if ( st.valueCount > expected["max_px"].asInt64() )
        {
            pass = false;
            delta = static_cast<double>( st.valueCount - expected["max_px"].asInt64() );
            message = QStringLiteral( "Changed pixels %1 above the known interval [%2, %3]" )
                        .arg( st.valueCount ).arg( expected["min_px"].asInt64() )
                        .arg( expected["max_px"].asInt64() );
        }
        return makeOutcome( pass, observed, expected, message, !pass, delta );
    }

    // classification_kappa / confusion_marginals without a forced outcome are
    // finalized by the caller (they need the confusion matrix state).
    return makeOutcome( false, Json::Value(), Json::Value(),
                        QStringLiteral( "Assertion was not evaluated" ) );
}

FinalOutcome finalizeClassification( const LabAssertion &a, const AssertionState &st )
{
    if ( !st.matrix )
        return makeOutcome( false, Json::Value(), Json::Value(),
                            QStringLiteral( "Confusion matrix was not built" ) );
    const Json::Value &params = a.params;
    const qint64 total = st.matrix->total();
    const double oa = st.matrix->overallAccuracy();
    const double kappa = st.matrix->kappa();

    if ( a.kind == QLatin1String( "classification_kappa" ) )
    {
        Json::Value observed;
        observed["oa"] = oa;
        observed["kappa"] = kappa;
        observed["total"] = static_cast<Json::Int64>( total );
        observed["unlabelled"] = static_cast<Json::Int64>( st.unlabelled );
        observed["truth_valid"] = static_cast<Json::Int64>( st.truthValid );
        Json::Value expected;
        expected["kappa_min"] = params["kappa_min"].asDouble();
        if ( params.isMember( "oa_min" ) )
            expected["oa_min"] = params["oa_min"].asDouble();
        QString message;
        bool pass = total > 0;
        double delta = 0.0;
        if ( !pass )
        {
            message = QStringLiteral( "No comparable truth/prediction pixels" );
        }
        else if ( params.isMember( "oa_min" ) && oa < params["oa_min"].asDouble() )
        {
            pass = false;
            delta = oa - params["oa_min"].asDouble();
            message = QStringLiteral( "Overall accuracy %1 below floor %2" )
                        .arg( oa ).arg( params["oa_min"].asDouble() );
        }
        else if ( kappa < params["kappa_min"].asDouble() )
        {
            pass = false;
            delta = kappa - params["kappa_min"].asDouble();
            message = QStringLiteral( "Kappa %1 below floor %2" )
                        .arg( kappa ).arg( params["kappa_min"].asDouble() );
        }
        return makeOutcome( pass, observed, expected, message, !pass, delta );
    }

    // confusion_marginals
    const double tolerance = params["tolerance"].asDouble();
    const Json::Value proportions = params["expected_proportions"];
    Json::Value observedProps( Json::objectValue );
    double maxDeviation = 0.0;
    bool pass = total > 0;
    QString worstLabel;
    for ( const auto &key : proportions.getMemberNames() )
    {
        // label column index: rules labels in declared order, then the
        // "#unlabelled" pseudo column appended by the walk
        int column = -1;
        const QStringList labels = st.matrix->labels();
        for ( int i = 0; i < labels.size(); ++i )
        {
            if ( labels.at( i ) == QString::fromStdString( key ) )
            {
                column = i;
                break;
            }
        }
        const double prop = column >= 0 && total > 0
                              ? static_cast<double>( st.matrix->predictedTotal( column ) )
                                  / static_cast<double>( total )
                              : 0.0;
        observedProps[key] = prop;
        const double deviation = std::abs( prop - proportions[key].asDouble() );
        if ( deviation > maxDeviation )
        {
            maxDeviation = deviation;
            worstLabel = QString::fromStdString( key );
        }
    }
    Json::Value observed;
    observed["proportions"] = observedProps;
    observed["total"] = static_cast<Json::Int64>( total );
    observed["unlabelled"] = static_cast<Json::Int64>( st.unlabelled );
    Json::Value expected;
    expected["expected_proportions"] = proportions;
    expected["tolerance"] = tolerance;
    QString message;
    if ( !pass )
        message = QStringLiteral( "No comparable truth/prediction pixels" );
    else if ( maxDeviation > tolerance )
        message = QStringLiteral( "Predicted proportion of class %1 deviates by %2 (tolerance %3)" )
                    .arg( worstLabel ).arg( maxDeviation ).arg( tolerance );
    pass = pass && maxDeviation <= tolerance;
    return makeOutcome( pass, observed, expected, message, total > 0, maxDeviation );
}

/// Loads the truth source for one classification assertion.  A missing or
/// unreadable truth reference is a LAB AUTHORING problem (usage class).
bool loadTruth( const LabAssertion &a, const QString &rulesDir, std::unique_ptr<RasterReader> *out,
                QString *usageError )
{
    const Json::Value truth = a.params["truth"];
    if ( truth.isMember( "inline" ) )
        return true;               // inline grids are read directly from params
    std::string pathStd = truth["path"].asString();
    QString path = QString::fromStdString( pathStd );
    if ( !QDir::isAbsolutePath( path ) )
        path = QDir( rulesDir ).filePath( path );
    if ( !QFile::exists( path ) )
    {
        *usageError = QStringLiteral( "Assertion \"%1\": truth raster does not exist: %2" )
                        .arg( a.id, path );
        return false;
    }
    try
    {
        *out = std::make_unique<RasterReader>( RasterReader::open( path.toUtf8().constData() ) );
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        *usageError = QStringLiteral( "Assertion \"%1\": cannot open truth raster %2: %3" )
                        .arg( a.id, path, QString::fromUtf8( e.what() ) );
        return false;
    }
    return true;
}

bool runClassificationWalks( const RasterReader &reader, const LabRuleSet &rules,
                             const QString &rulesDir, std::size_t maxBytes,
                             std::map<QString, AssertionState> &states,
                             std::map<QString, FinalOutcome> &forced, GradeContext &ctx,
                             QString *usageError )
{
    const RasterMetadata &meta = reader.metadata();
    for ( const LabAssertion &a : rules.assertions )
    {
        if ( a.kind != QLatin1String( "classification_kappa" )
             && a.kind != QLatin1String( "confusion_marginals" ) )
        {
            continue;
        }
        AssertionState &st = states[a.id];

        // labels + the internal unlabelled pseudo column
        std::vector<int> labelCodes;
        QStringList labelStrings;
        for ( const auto &item : a.params["labels"] )
        {
            labelCodes.push_back( item.asInt() );
            labelStrings << QString::number( item.asInt() );
        }
        const QString unlabelledLabel = QStringLiteral( "#unlabelled" );
        labelStrings << unlabelledLabel;
        st.matrix = std::make_unique<sicnu::experiment::ConfusionMatrix>(
          labelStrings, static_cast<qint64>( labelStrings.size() ) );
        const int unlabelledColumn = static_cast<int>( labelStrings.size() ) - 1;

        // truth source
        std::unique_ptr<RasterReader> truthReader;
        std::vector<int> inlineGrid;
        int truthWidth = 0, truthHeight = 0;
        const BandInfo *truthBand = nullptr;
        if ( a.params["truth"].isMember( "inline" ) )
        {
            const Json::Value rows = a.params["truth"]["inline"];
            truthHeight = static_cast<int>( rows.size() );
            for ( const auto &row : rows )
            {
                if ( !row.isArray() )
                {
                    *usageError = QStringLiteral( "Assertion \"%1\": truth.inline rows must be arrays" ).arg( a.id );
                    return false;
                }
                truthWidth = static_cast<int>( row.size() );
                for ( const auto &cell : row )
                    inlineGrid.push_back( cell.asInt() );
            }
        }
        else
        {
            if ( !loadTruth( a, rulesDir, &truthReader, usageError ) )
                return false;
            truthWidth = truthReader->metadata().width;
            truthHeight = truthReader->metadata().height;
            truthBand = &truthReader->metadata().bands.at( 0 );
            if ( truthReader->metadata().bandCount < 1 )
            {
                *usageError = QStringLiteral( "Assertion \"%1\": truth raster has no bands" ).arg( a.id );
                return false;
            }
        }

        const int band = a.params["band"].asInt();
        const bool hasNoDataClass = a.params.isMember( "nodata_class" );
        const int nodataClass = hasNoDataClass ? a.params["nodata_class"].asInt() : 0;

        // grid compatibility is a graded (blocking-capable) failure, not a crash
        if ( band > meta.bandCount )
        {
            forced[a.id] = makeOutcome( false, bandAvailabilityObserved( band, meta ), Json::Value(),
                                        QStringLiteral( "Artifact lacks band %1" ).arg( band ) );
            continue;
        }
        if ( truthWidth != meta.width || truthHeight != meta.height )
        {
            Json::Value observed;
            observed["truth_width"] = truthWidth;
            observed["truth_height"] = truthHeight;
            observed["artifact_width"] = meta.width;
            observed["artifact_height"] = meta.height;
            forced[a.id] = makeOutcome( false, observed, Json::Value(),
                                        QStringLiteral( "Truth grid does not match the artifact grid" ) );
            continue;
        }

        int tileW = 0, tileH = 0;
        if ( !planTileGeometry( meta.width, meta.height, 1, maxBytes, &tileW, &tileH, &ctx.error ) )
            return false;
        RasterWindow full;
        full.width = meta.width;
        full.height = meta.height;

        const BandInfo &artifactBand = meta.bands.at( static_cast<std::size_t>( band - 1 ) );
        auto sink = [&]( const sicnu::geo::TileSlice &slice, const std::vector<double> &buffer )
        {
            std::vector<double> truthVals;
            if ( truthReader )
            {
                RasterWindow window;
                window.xOff = slice.xOff;
                window.yOff = slice.yOff;
                window.width = slice.width;
                window.height = slice.height;
                truthVals = truthReader->readWindow( { 1 }, window, maxBytes );
            }
            const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;
            for ( std::size_t p = 0; p < plane; ++p )
            {
                const double v = buffer[p];
                if ( isNoData( artifactBand, v ) || isNonFinite( v ) )
                    continue;
                double tv = 0.0;
                if ( truthReader )
                    tv = truthVals[p];
                else
                    tv = static_cast<double>(
                      inlineGrid.at( static_cast<std::size_t>( slice.yOff + static_cast<int>( p / slice.width ) )
                                       * truthWidth
                                     + slice.xOff + static_cast<int>( p % slice.width ) ) );
                if ( truthBand && ( isNoData( *truthBand, tv ) || isNonFinite( tv ) ) )
                    continue;
                if ( hasNoDataClass && tv == static_cast<double>( nodataClass ) )
                    continue;
                ++st.truthValid;

                const auto truthIt = std::find( labelCodes.begin(), labelCodes.end(),
                                                static_cast<int>( tv ) );
                double pred = v;
                const int predRound = static_cast<int>( std::lround( pred ) );
                const auto predIt = std::find( labelCodes.begin(), labelCodes.end(), predRound );
                int predColumn = unlabelledColumn;
                if ( predIt == labelCodes.end() || std::abs( pred - predRound ) > 1e-9 )
                {
                    ++st.unlabelled;
                }
                else
                {
                    predColumn = static_cast<int>( predIt - labelCodes.begin() );
                }
                int truthRow = unlabelledColumn;
                if ( truthIt == labelCodes.end() )
                {
                    ++st.unlabelled;
                }
                else
                {
                    truthRow = static_cast<int>( truthIt - labelCodes.begin() );
                }
                st.matrix->increment( truthRow, predColumn );
            }
        };
        try
        {
            const TilePlan plan = sicnu::geo::planTileWalk( meta, full, tileW, tileH );
            reader.iterateTiles( plan, { band }, sink, {} );
        }
        catch ( const sicnu::geo::GeoError &e )
        {
            ctx.error = QString::fromUtf8( e.what() );
            return false;
        }
    }
    return true;
}

} // namespace lab_grading

OutputVerifier::LabGradeResult OutputVerifier::gradeForTeaching( const QString &labIdOrRulesPath, const QString &artifactPath,
                                                 const LabGradeOptions &options ) const
{
    using namespace lab_grading;

    LabGradeResult result;
    result.artifactPath = artifactPath;
    result.passingScore = 60.0;
    result.verdict = QStringLiteral( "unverifiable" );
    result.graded = false;

    // ---- rules resolution (usage class) -----------------------------------
    QString error;
    const QString rulesPath = resolveRulesPath( labIdOrRulesPath, options.rulesDir );
    if ( rulesPath.isEmpty() )
    {
        result.error = QStringLiteral( "Unknown lab \"%1\": no <lab_id>.rules.json found" ).arg( labIdOrRulesPath );
        result.errorClass = QStringLiteral( "usage" );
        return result;
    }
    LabRuleSet rules;
    if ( !parseRules( rulesPath, &rules, &error ) )
    {
        result.error = error;
        result.errorClass = QStringLiteral( "usage" );
        return result;
    }
    result.rulesPath = rules.path;
    result.labId = rules.labId;
    result.passingScore = rules.passingScore;

    // ---- artifact open (usage when missing; artifact class otherwise) -----
    if ( artifactPath.trimmed().isEmpty() || !QFile::exists( artifactPath ) )
    {
        result.error = QStringLiteral( "Artifact does not exist: %1" ).arg( artifactPath );
        result.errorClass = QStringLiteral( "usage" );
        return result;
    }
    result.artifactPath = QFileInfo( artifactPath ).absoluteFilePath();

    std::unique_ptr<RasterReader> reader;
    const RasterMetadata *meta = nullptr;
    try
    {
        reader = std::make_unique<RasterReader>( RasterReader::open( artifactPath.toUtf8().constData() ) );
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        result.error = QStringLiteral( "Cannot open artifact as a raster: %1" ).arg( e.what() );
        result.errorClass = QStringLiteral( "artifact" );
        return result;
    }
    meta = &reader->metadata();
    if ( meta->bandCount <= 0 )
    {
        result.error = QStringLiteral( "Artifact has zero bands" );
        result.errorClass = QStringLiteral( "artifact" );
        return result;
    }

    // ---- per-assertion states ---------------------------------------------
    std::map<QString, AssertionState> states;
    for ( const LabAssertion &a : rules.assertions )
        states[a.id];

    GradeContext ctx;
    ctx.meta = meta;
    ctx.maxBytes = options.maxBytes;

    // ---- single content walk (feeds every value kernel in one streaming
    // pass under the byte budget) --------------------------------------------
    ContentWalk walk( *reader, rules, ctx, states );
    if ( !ctx.error.isEmpty() || !walk.run() )
    {
        result.error = ctx.error;
        result.errorClass = QStringLiteral( "artifact" );
        return result;
    }

    // ---- classification paired walks (truth rasters / inline grids) --------
    std::map<QString, FinalOutcome> classificationForced;
    QString usageError;
    if ( !runClassificationWalks( *reader, rules, QFileInfo( rules.path ).absolutePath(),
                                  options.maxBytes, states, classificationForced, ctx,
                                  &usageError ) )
    {
        if ( !usageError.isEmpty() )
        {
            result.error = usageError;
            result.errorClass = QStringLiteral( "usage" );
        }
        else
        {
            result.error = ctx.error;
            result.errorClass = QStringLiteral( "artifact" );
        }
        return result;
    }

    // ---- finalize: evidence, deductions, score ------------------------------
    double score = 100.0;
    bool capped = false;
    for ( const LabAssertion &a : rules.assertions )
    {
        const AssertionState &st = states[a.id];
        const auto forced = classificationForced.find( a.id );
        FinalOutcome outcome;
        if ( forced != classificationForced.end() )
            outcome = forced->second;
        else if ( a.kind == QLatin1String( "classification_kappa" )
                  || a.kind == QLatin1String( "confusion_marginals" ) )
            outcome = finalizeClassification( a, st );
        else
            outcome = finalizeAssertion( a, st, *meta );

        LabEvidence evidence;
        evidence.assertionId = a.id;
        evidence.kind = a.kind;
        evidence.passed = outcome.passed;
        evidence.observed = roundJson( outcome.observed );
        evidence.expected = roundJson( outcome.expected );
        result.evidence.push_back( evidence );

        if ( !outcome.passed )
        {
            LabDeduction d;
            d.assertionId = a.id;
            d.kind = a.kind;
            d.severity = a.severity;
            d.weight = a.weight;
            d.delta = outcome.hasDelta ? outcome.delta : std::numeric_limits<double>::quiet_NaN();
            d.message = outcome.message;
            d.observed = evidence.observed;
            d.expected = evidence.expected;
            result.deductions.push_back( d );
            score -= a.weight;
            if ( a.severity == QLatin1String( "blocking" ) )
                capped = true;
        }
    }

    if ( capped )
        score = std::min( score, rules.passingScore - 1.0 );
    score = std::max( 0.0, score );
    result.score = round12( score );
    result.cappedByBlocking = capped;
    result.verdict = ( !capped && result.score >= rules.passingScore ) ? QStringLiteral( "pass" )
                                                                       : QStringLiteral( "fail" );
    result.graded = true;

    // ---- summary ------------------------------------------------------------
    Json::Value summary;
    summary["width"] = meta->width;
    summary["height"] = meta->height;
    summary["band_count"] = meta->bandCount;
    summary["crs"] = meta->crs.authid;
    if ( meta->hasGeotransform )
    {
        summary["pixel_size_x"] = round12( meta->resolutionX );
        summary["pixel_size_y"] = round12( meta->resolutionY );
    }
    summary["max_bytes_budget"] = static_cast<Json::Int64>( options.maxBytes );
    summary["tile_width"] = ctx.tileWidth;
    summary["tile_height"] = ctx.tileHeight;
    summary["tiles"] = static_cast<Json::Int64>( ctx.tiles );
    for ( const LabAssertion &a : rules.assertions )
    {
        if ( a.kind != QLatin1String( "nodata_ratio" ) )
            continue;
        const AssertionState &st = states[a.id];
        if ( st.totalPixels > 0 )
        {
            summary["valid_pixels"] = static_cast<Json::Int64>( st.totalPixels - st.nodataPixels );
            summary["nodata_pixels"] = static_cast<Json::Int64>( st.nodataPixels );
            summary["nodata_ratio"] = round12( static_cast<double>( st.nodataPixels )
                                               / static_cast<double>( st.totalPixels ) );
        }
        break;
    }
    result.summary = summary;

    // digest over the canonical body (no timestamp inside — autonomy default 6)
    const std::string canonical = Json::writeString(
      []()
      {
          Json::StreamWriterBuilder b;
          b["indentation"] = "";
          b["commentStyle"] = "None";
          return b;
      }(),
      result.toBodyJson() );
    result.digest = QString::fromLatin1(
      QCryptographicHash::hash( canonical.data(), QCryptographicHash::Sha256 ).toHex() );
    return result;
}

Json::Value OutputVerifier::LabDeduction::toJson() const
{
  Json::Value json;
  json["assertion_id"] = assertionId.toStdString();
  json["kind"] = kind.toStdString();
  json["severity"] = severity.toStdString();
  json["weight"] = lab_grading::round12( weight );
  json["observed"] = observed;
  json["expected"] = expected;
  json["delta"] = std::isnan( delta ) ? Json::Value( Json::nullValue )
                                      : Json::Value( lab_grading::round12( delta ) );
  json["message"] = message.toStdString();
  return json;
}

Json::Value OutputVerifier::LabEvidence::toJson() const
{
  Json::Value json;
  json["assertion_id"] = assertionId.toStdString();
  json["kind"] = kind.toStdString();
  json["passed"] = passed;
  json["observed"] = observed;
  json["expected"] = expected;
  return json;
}

Json::Value OutputVerifier::LabGradeResult::toBodyJson() const
{
  Json::Value body;
  body["lab_id"] = labId.toStdString();
  body["artifact"] = artifactPath.toStdString();
  body["rules"] = rulesPath.toStdString();
  body["verdict"] = verdict.toStdString();
  body["score"] = lab_grading::round12( score );
  body["passing_score"] = lab_grading::round12( passingScore );
  body["capped_by_blocking"] = cappedByBlocking;
  Json::Value deductionJson( Json::arrayValue );
  for ( const LabDeduction &d : deductions )
    deductionJson.append( d.toJson() );
  body["deductions"] = deductionJson;
  Json::Value evidenceJson( Json::arrayValue );
  for ( const LabEvidence &e : evidence )
    evidenceJson.append( e.toJson() );
  body["evidence"] = evidenceJson;
  body["summary"] = summary;
  if ( !graded )
    body["error"] = error.toStdString();
  return body;
}

Json::Value OutputVerifier::LabGradeResult::toJson( const QString &generatedUtc ) const
{
  const Json::Value body = toBodyJson();
  const std::string canonical = Json::writeString(
    []()
    {
      Json::StreamWriterBuilder b;
      b["indentation"] = "";
      b["commentStyle"] = "None";
      return b;
    }(),
    body );
  const QByteArray digest = QCryptographicHash::hash( canonical.data(), QCryptographicHash::Sha256 ).toHex();

  Json::Value document;
  document["schema"] = lab_grading::kGradeSchemaName;
  document["digest"] = QString::fromLatin1( digest ).toStdString();
  document["generated_utc"] = generatedUtc.toStdString();
  document["report"] = body;
  return document;
}

} // namespace sicnu::agent
