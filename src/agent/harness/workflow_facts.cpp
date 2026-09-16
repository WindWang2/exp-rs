// src/agent/harness/workflow_facts.cpp
#include "workflow_facts.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

#include <QCryptographicHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <set>

namespace sicnu::agent::harness::wfacts {

namespace {

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

void stampStatus( Json::Value &obj, const char *key, const char *status )
{
  obj["fact_status"][key] = status;
}

/// Median of an already-sorted vector.
double medianOfSorted( const std::vector<long long> &sorted )
{
  if ( sorted.empty() )
    return 0.0;
  const size_t n = sorted.size();
  if ( n % 2 == 1 )
    return static_cast<double>( sorted[n / 2] );
  return ( static_cast<double>( sorted[n / 2 - 1] ) + static_cast<double>( sorted[n / 2] ) ) / 2.0;
}

/// Closed geographic CRS authids that carry degree units. Small by design:
/// any authid outside the table without WKT stays unknown — never guessed.
bool authidIsDegree( const std::string &authidLower )
{
  static const char *kDegreeAuthids[] = {
    "epsg:4326", "epsg:4269", "epsg:4322", "epsg:4258", "crs:84", "ogc:crs84",
  };
  for ( const char *known : kDegreeAuthids )
    if ( authidLower == known )
      return true;
  return false;
}

/// Unit names from the WKT of an understanding `crs` document, in order.
/// WKT1 layout: GEOGCS carries one angular UNIT; PROJCS nests that GEOGCS
/// and appends its own linear UNIT at the end — so the LAST UNIT is the
/// linear one for a PROJCS and the first (only) one for a GEOGCS.
std::vector<std::string> wktUnits( const std::string &wktRaw )
{
  const std::string wkt = lowered( wktRaw );
  std::vector<std::string> units;
  std::string::size_type pos = 0;
  while ( ( pos = wkt.find( "unit[\"", pos ) ) != std::string::npos )
  {
    const std::string::size_type nameStart = pos + 6;
    const std::string::size_type nameEnd = wkt.find( '"', nameStart );
    if ( nameEnd == std::string::npos )
      break;
    units.push_back( wkt.substr( nameStart, nameEnd - nameStart ) );
    pos = nameEnd;
  }
  return units;
}

std::string wktLinearUnit( const std::string &wktRaw )
{
  const std::string wkt = lowered( wktRaw );
  const std::vector<std::string> units = wktUnits( wkt );
  if ( units.empty() )
    return std::string();
  const bool projected = wkt.find( "projcs" ) != std::string::npos;
  return projected ? units.back() : units.front();
}

/// "metre" | "degree" | "" from an understanding document's `crs` slot
/// (string authid, or {authid, wkt} object as rasters carry it).
std::string crsUnitOf( const Json::Value &understanding )
{
  const Json::Value crs = understanding.get( "crs", Json::Value() );
  if ( crs.isObject() )
  {
    const std::string wkt = crs.get( "wkt", "" ).asString();
    if ( !wkt.empty() )
    {
      const std::string unit = wktLinearUnit( wkt );
      if ( unit.find( "degree" ) != std::string::npos )
        return "degree";
      if ( unit.find( "metre" ) != std::string::npos ||
           unit.find( "meter" ) != std::string::npos )
        return "metre";
      return std::string();
    }
    return authidIsDegree( lowered( crs.get( "authid", "" ).asString() ) ) ? "degree"
                                                                          : std::string();
  }
  if ( crs.isString() )
  {
    const std::string text = crs.asString();
    if ( !text.empty() )
    {
      const std::string loweredText = lowered( text );
      if ( authidIsDegree( loweredText ) )
        return "degree";
      // A full WKT string riding the string slot: apply the same rule.
      if ( loweredText.find( "projcs" ) != std::string::npos ||
           loweredText.find( "geogcs" ) != std::string::npos )
        return wktLinearUnit( text ).find( "degree" ) != std::string::npos
                 ? "degree"
                 : std::string();
    }
  }
  return std::string();
}

/// Collects the acquisition-date strings an understanding document carries.
void collectUnderstandingDates( const Json::Value &understanding,
                                std::vector<std::string> &raw )
{
  auto pushScalar = [&]( const Json::Value &value ) {
    if ( value.isString() && !value.asString().empty() )
      raw.push_back( value.asString() );
  };
  pushScalar( understanding.get( "acquisition_time", Json::Value() ) );
  pushScalar( understanding.get( "SICNU_ACQUISITION_DATE", Json::Value() ) );
  for ( const char *key : { "temporal", "temporal_facts" } )
  {
    if ( understanding.isMember( key ) && understanding[key].isObject() )
      pushScalar( understanding[key].get( "acquisition_time", Json::Value() ) );
  }
  // Folded-in date arrays (callers may attach collection-level coverage;
  // `dates` is the closed kFactKeys entry the planner grounds collections
  // into).
  for ( const char *key : { "dates" } )
  {
    if ( understanding.isMember( key ) && understanding[key].isArray() )
    {
      for ( const Json::Value &entry : understanding[key] )
      {
        if ( entry.isString() )
          raw.push_back( entry.asString() );
        else if ( entry.isObject() )
          pushScalar( entry.get( "acquisition_time", entry.get( "time",
                                                                entry.get( "date",
                                                                          Json::Value() ) ) ) );
      }
    }
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

Json::Value workflowFactsLimits()
{
  Json::Value limits( Json::objectValue );
  limits["max_dates"] = FactsLimits::kMaxDates;
  limits["max_masks"] = FactsLimits::kMaxMasks;
  limits["max_text_chars"] = FactsLimits::kMaxTextChars;
  limits["max_scenes"] = FactsLimits::kMaxScenes;
  return limits;
}

// ---------------------------------------------------------------------------
// Time parsing
// ---------------------------------------------------------------------------

ParsedInstant parseInstant( const std::string &text )
{
  ParsedInstant out;
  QString q = QString::fromStdString( text ).trimmed();
  if ( q.isEmpty() )
    return out;

  // Date-only first: a date is a date; forcing a time-of-day would
  // fabricate precision.
  const QDate date = QDate::fromString( q, "yyyy-MM-dd" );
  if ( date.isValid() )
  {
    out.epochSeconds = QDateTime( date, QTime( 0, 0 ), QTimeZone::utc() ).toSecsSinceEpoch();
    out.dateOnly = true;
    out.tz = TzAssumption::Explicit; // a date carries no offset to assume
    out.ok = true;
    return out;
  }

  // Normalize the separator, then peel off zone + fractional seconds so the
  // naive part can be re-assembled with an EXPLICIT timezone (naive sources
  // are UTC by harness convention and stamped as an assumption).
  QString iso = q;
  iso.replace( QLatin1Char( ' ' ), QLatin1Char( 'T' ) );

  static const QRegularExpression zonePattern( QStringLiteral( "(Z|[+-]\\d{2}:?\\d{2})$" ) );
  const QRegularExpressionMatch zoneMatch = zonePattern.match( iso );
  bool explicitZone = false;
  int offsetSeconds = 0;
  if ( zoneMatch.hasMatch() )
  {
    const QString captured = zoneMatch.captured( 1 );
    explicitZone = true;
    if ( captured == QLatin1String( "Z" ) )
    {
      offsetSeconds = 0;
    }
    else
    {
      QString digits = captured;
      digits.remove( QLatin1Char( ':' ) );
      const int hours = digits.mid( 1, 2 ).toInt();
      const int minutes = digits.mid( 3, 2 ).toInt();
      offsetSeconds = ( hours * 3600 + minutes * 60 ) * ( captured.startsWith( QLatin1Char( '-' ) ) ? -1 : 1 );
    }
    iso = iso.left( zoneMatch.capturedStart() );
  }

  int msec = 0;
  static const QRegularExpression fractionPattern( QStringLiteral( "\\.(\\d+)" ) );
  const QRegularExpressionMatch fractionMatch = fractionPattern.match( iso );
  if ( fractionMatch.hasMatch() )
  {
    const QString digits = fractionMatch.captured( 1 ).left( 3 );
    msec = digits.toInt();
    if ( digits.size() < 3 )
      msec *= static_cast<int>( std::pow( 10.0, 3.0 - static_cast<double>( digits.size() ) ) );
    iso = iso.left( fractionMatch.capturedStart() );
  }

  QDateTime naive = QDateTime::fromString( iso, "yyyy-MM-ddTHH:mm:ss" );
  if ( !naive.isValid() )
    naive = QDateTime::fromString( iso, "yyyy-MM-ddTHH:mm" );
  if ( !naive.isValid() )
    return out;
  naive = naive.addMSecs( msec );

  if ( explicitZone )
  {
    out.epochSeconds =
      QDateTime( naive.date(), naive.time(), QTimeZone::utc() ).toSecsSinceEpoch() -
      offsetSeconds;
    out.tz = TzAssumption::Explicit;
  }
  else
  {
    out.epochSeconds =
      QDateTime( naive.date(), naive.time(), QTimeZone::utc() ).toSecsSinceEpoch();
    out.tz = TzAssumption::NaiveAsUtc;
  }
  out.dateOnly = false;
  out.ok = true;
  return out;
}

std::string formatInstant( long long epochSeconds, bool dateOnly )
{
  const QDateTime dateTime = QDateTime::fromSecsSinceEpoch( epochSeconds, QTimeZone::utc() );
  if ( dateOnly )
    return dateTime.date().toString( "yyyy-MM-dd" ).toStdString();
  return dateTime.toString( "yyyy-MM-ddTHH:mm:ssZ" ).toStdString();
}

namespace regularity {
bool isKnownRegularity( const std::string &value )
{
  return value == kNone || value == kSingle || value == kRegular || value == kNearRegular ||
         value == kIrregular || value == kUnknown;
}
} // namespace regularity

TemporalCadenceFacts temporalCadenceFromDates( const Json::Value &dates )
{
  TemporalCadenceFacts out;
  if ( !dates.isArray() )
  {
    out.regularity = regularity::kUnknown;
    return out;
  }

  // Parse + keep precision; dedup by epoch keeping the COARSER precision
  // when two entries collide (a mixed list stays honest about its coarsest
  // member).
  std::vector<std::pair<long long, bool>> instants; // (epoch, dateOnly)
  int unboundedCount = 0;
  for ( const Json::Value &entry : dates )
  {
    std::string text;
    if ( entry.isString() )
      text = entry.asString();
    else if ( entry.isObject() )
    {
      Json::Value time = entry.get( "acquisition_time",
                                    entry.get( "time", entry.get( "date", Json::Value() ) ) );
      if ( time.isString() )
        text = time.asString();
    }
    if ( text.empty() )
      continue;
    ++unboundedCount;
    if ( instants.size() >= static_cast<size_t>( FactsLimits::kMaxDates ) )
      continue; // honest truncation below
    const ParsedInstant parsed = parseInstant( text );
    if ( !parsed.ok )
    {
      if ( out.unparseable.size() < static_cast<size_t>( FactsLimits::kMaxDates ) )
        out.unparseable.push_back( text );
      continue;
    }
    // Merge precision per epoch.
    bool merged = false;
    for ( auto &existing : instants )
    {
      if ( existing.first == parsed.epochSeconds )
      {
        existing.second = existing.second && parsed.dateOnly;
        merged = true;
        break;
      }
    }
    if ( !merged )
      instants.emplace_back( parsed.epochSeconds, parsed.dateOnly );
  }
  out.truncated = unboundedCount > FactsLimits::kMaxDates;
  std::sort( instants.begin(), instants.end() );

  out.count = static_cast<int>( instants.size() );
  if ( instants.empty() )
  {
    out.regularity = regularity::kNone;
    return out;
  }
  out.first = formatInstant( instants.front().first, instants.front().second );
  out.last = formatInstant( instants.back().first, instants.back().second );
  out.spanSeconds = instants.back().first - instants.front().first;
  if ( out.count == 1 )
  {
    out.regularity = regularity::kSingle;
    return out;
  }

  std::vector<long long> gaps;
  gaps.reserve( instants.size() - 1 );
  for ( size_t i = 1; i < instants.size(); ++i )
    gaps.push_back( instants[i].first - instants[i - 1].first );

  const double median = medianOfSorted( gaps );
  out.cadenceDays = median / 86400.0;

  int deviations = 0;
  int exact = 0;
  for ( const long long gap : gaps )
  {
    if ( std::llabs( gap - static_cast<long long>( std::llround( median ) ) ) <= 60 )
      ++exact;
    if ( median > 0 && ( static_cast<double>( gap ) < 0.75 * median ||
                         static_cast<double>( gap ) > 1.25 * median ) )
      ++deviations;
  }
  out.gapDeviations = deviations;

  if ( exact == static_cast<int>( gaps.size() ) )
    out.regularity = regularity::kRegular;
  else if ( static_cast<double>( deviations ) <= 0.2 * static_cast<double>( gaps.size() ) )
    out.regularity = regularity::kNearRegular;
  else
    out.regularity = regularity::kIrregular;

  // Cadence label — closed rules, hand-computable:
  //  monthly: every consecutive pair advances exactly one calendar month and
  //           the day-of-month moves by at most one day;
  //  annual:  every gap is 365 or 366 days AND each pair advances one year;
  //  "<N>d":  median gap is an integer number of days (0.01 tolerance).
  bool monthly = true;
  bool annual = true;
  for ( size_t i = 1; i < instants.size(); ++i )
  {
    const QDate previous =
      QDateTime::fromSecsSinceEpoch( instants[i - 1].first, QTimeZone::utc() ).date();
    const QDate current =
      QDateTime::fromSecsSinceEpoch( instants[i].first, QTimeZone::utc() ).date();
    const int monthDelta =
      ( current.year() - previous.year() ) * 12 + ( current.month() - previous.month() );
    if ( monthDelta != 1 || std::abs( current.day() - previous.day() ) > 1 )
      monthly = false;
    const long long gap = instants[i].first - instants[i - 1].first;
    const int yearDelta = current.year() - previous.year();
    if ( !( ( gap == 365 * 86400 || gap == 366 * 86400 ) && yearDelta == 1 ) )
      annual = false;
  }
  if ( monthly )
    out.cadenceLabel = "monthly";
  else if ( annual )
    out.cadenceLabel = "annual";
  else
  {
    const double rounded = std::round( out.cadenceDays );
    if ( rounded >= 1.0 && std::abs( out.cadenceDays - rounded ) < 0.01 )
      out.cadenceLabel = std::to_string( static_cast<long long>( rounded ) ) + "d";
  }
  return out;
}

TemporalCadenceFacts temporalCadenceFromUnderstanding( const Json::Value &understanding )
{
  std::vector<std::string> raw;
  collectUnderstandingDates( understanding, raw );
  Json::Value array( Json::arrayValue );
  for ( const std::string &text : raw )
    array.append( text );
  return temporalCadenceFromDates( array );
}

Json::Value temporalDatesFromCollectionDescriptor( const Json::Value &descriptor )
{
  if ( !descriptor.isObject() || !descriptor.isMember( "scenes" ) ||
       !descriptor["scenes"].isArray() )
    return Json::Value();
  Json::Value dates( Json::arrayValue );
  int seen = 0;
  for ( const Json::Value &scene : descriptor["scenes"] )
  {
    if ( seen >= FactsLimits::kMaxScenes )
      break;
    ++seen;
    if ( scene.isObject() )
    {
      Json::Value time = scene.get( "acquisition_time",
                                    scene.get( "time", scene.get( "date", Json::Value() ) ) );
      if ( time.isString() && !time.asString().empty() )
        dates.append( time.asString() );
    }
    else if ( scene.isString() )
    {
      // Path-only entries carry no time — honest skip (times array is the
      // parallel channel some descriptors use).
      continue;
    }
  }
  // Parallel `times` array (temporal:create_collection input shape) — only
  // fills slots the scene objects did not declare, positionally.
  if ( descriptor.isMember( "times" ) && descriptor["times"].isArray() )
  {
    int index = 0;
    for ( const Json::Value &time : descriptor["times"] )
    {
      if ( index >= static_cast<int>( dates.size() ) )
        break;
      if ( time.isString() && !time.asString().empty() )
        dates[index] = time.asString();
      ++index;
    }
  }
  return dates;
}

Json::Value TemporalCadenceFacts::toJson( const std::string &status ) const
{
  Json::Value json( Json::objectValue );
  json["count"] = count;
  json["truncated"] = truncated;
  json["first"] = first;
  json["last"] = last;
  json["span_seconds"] = static_cast<Json::Int64>( spanSeconds );
  json["regularity"] = regularity;
  if ( count >= 2 )
    json["cadence_days"] = cadenceDays;
  if ( !cadenceLabel.empty() )
    json["cadence_label"] = cadenceLabel;
  json["gap_deviations"] = gapDeviations;
  Json::Value unparseableJson( Json::arrayValue );
  for ( const std::string &text : unparseable )
    unparseableJson.append( text );
  json["unparseable"] = unparseableJson;

  Json::Value factStatus( Json::objectValue );
  factStatus["count"] = regularity == regularity::kUnknown ? "unknown" : status.c_str();
  factStatus["first"] = first.empty() ? "unknown" : status.c_str();
  factStatus["last"] = last.empty() ? "unknown" : status.c_str();
  factStatus["span_seconds"] = count >= 2 ? "derived" : "unknown";
  factStatus["regularity"] = regularity == regularity::kUnknown ? "unknown" : "derived";
  factStatus["cadence_days"] = count >= 2 ? "derived" : "unknown";
  factStatus["cadence_label"] = cadenceLabel.empty() ? "unknown" : "derived";
  factStatus["gap_deviations"] = count >= 2 ? "derived" : "unknown";
  factStatus["unparseable"] = unparseable.empty() ? "unknown" : "observed";
  json["fact_status"] = factStatus;
  return json;
}

// ---------------------------------------------------------------------------
// Spatial resolution / extent
// ---------------------------------------------------------------------------

namespace resolution_class {
bool isKnownResolutionClass( const std::string &value )
{
  return value == kFine || value == kMedium || value == kCoarse || value == kUnknownMeters ||
         value == kUnknown;
}
} // namespace resolution_class

ResolutionFacts spatialResolutionFacts( const Json::Value &understanding )
{
  ResolutionFacts out;
  const Json::Value pixelSize = understanding.get( "pixel_size", Json::Value() );
  // Both wire shapes occur in the wild: the inspect tools emit {x, y}
  // objects, declared documents often carry [x, y] arrays (band_facts
  // gridFacts accepts the array form) — the fact model reads both or
  // neither.
  double px = 0.0;
  double py = 0.0;
  bool present = false;
  if ( pixelSize.isObject() && pixelSize.isMember( "x" ) && pixelSize.isMember( "y" ) &&
       pixelSize["x"].isNumeric() && pixelSize["y"].isNumeric() )
  {
    px = pixelSize["x"].asDouble();
    py = pixelSize["y"].asDouble();
    present = true;
  }
  else if ( pixelSize.isArray() && pixelSize.size() == 2 && pixelSize[0].isNumeric() &&
            pixelSize[1].isNumeric() )
  {
    px = pixelSize[0].asDouble();
    py = pixelSize[1].asDouble();
    present = true;
  }
  if ( present )
  {
    out.pixelSizeX = px;
    out.pixelSizeY = py;
    out.pixelSizePresent = true;
    const double mean = ( std::abs( out.pixelSizeX ) + std::abs( out.pixelSizeY ) ) / 2.0;
    const double diff =
      std::abs( std::abs( out.pixelSizeX ) - std::abs( out.pixelSizeY ) );
    out.anisotropic = mean > 0.0 && diff > 1e-9 * std::max( 1.0, mean );

    out.crsUnit = crsUnitOf( understanding );
    if ( out.crsUnit == "metre" )
    {
      const double metres = mean;
      if ( metres <= 10.0 )
        out.resolutionClass = resolution_class::kFine;
      else if ( metres <= 30.0 )
        out.resolutionClass = resolution_class::kMedium;
      else
        out.resolutionClass = resolution_class::kCoarse;
    }
    else if ( out.crsUnit == "degree" )
    {
      out.resolutionClass = resolution_class::kUnknownMeters;
    }
    else
    {
      out.resolutionClass = resolution_class::kUnknownMeters;
    }
  }
  else
  {
    out.resolutionClass = resolution_class::kUnknown;
  }

  const Json::Value extent = understanding.get( "extent", Json::Value() );
  if ( extent.isObject() && extent.isMember( "minX" ) && extent.isMember( "maxX" ) &&
       extent.isMember( "minY" ) && extent.isMember( "maxY" ) && extent["minX"].isNumeric() &&
       extent["maxX"].isNumeric() && extent["minY"].isNumeric() && extent["maxY"].isNumeric() )
  {
    out.extentPresent = true;
    out.xmin = extent["minX"].asDouble();
    out.xmax = extent["maxX"].asDouble();
    out.ymin = extent["minY"].asDouble();
    out.ymax = extent["maxY"].asDouble();
    out.extentValid = out.xmin < out.xmax && out.ymin < out.ymax;
  }
  return out;
}

Json::Value ResolutionFacts::toJson() const
{
  Json::Value json( Json::objectValue );
  if ( pixelSizePresent )
  {
    json["pixel_size"] = Json::Value( Json::objectValue );
    json["pixel_size"]["x"] = pixelSizeX;
    json["pixel_size"]["y"] = pixelSizeY;
    json["pixel_size"]["anisotropic"] = anisotropic;
    json["crs_unit"] = crsUnit.empty() ? "unknown" : crsUnit;
    json["resolution_class"] = resolutionClass;
  }
  else
  {
    json["resolution_class"] = resolution_class::kUnknown;
  }
  if ( extentPresent )
  {
    Json::Value extent( Json::objectValue );
    extent["minX"] = xmin;
    extent["maxX"] = xmax;
    extent["minY"] = ymin;
    extent["maxY"] = ymax;
    extent["valid"] = extentValid;
    json["extent"] = extent;
  }

  Json::Value factStatus( Json::objectValue );
  factStatus["pixel_size"] = pixelSizePresent ? "observed" : "unknown";
  factStatus["crs_unit"] = crsUnit.empty() ? "unknown" : "derived";
  factStatus["resolution_class"] =
    resolutionClass == resolution_class::kUnknown ? "unknown" : "derived";
  factStatus["extent"] = extentPresent ? ( extentValid ? "observed" : "observed" ) : "unknown";
  json["fact_status"] = factStatus;
  return json;
}

// ---------------------------------------------------------------------------
// Quality masks
// ---------------------------------------------------------------------------

bool isMaskRoleName( const std::string &role )
{
  static const char *kMaskRoles[] = {
    "mask", "qa", "quality", "cloud", "cloud_mask", "cloud_and_shadow", "cloud_shadow",
    "snow", "validity",
  };
  const std::string loweredRole = lowered( role );
  for ( const char *known : kMaskRoles )
    if ( loweredRole == known )
      return true;
  return false;
}

QualityMaskFacts qualityMaskFacts( const Json::Value &understanding )
{
  QualityMaskFacts out;
  const Json::Value masks = understanding.get( "quality_masks", Json::Value() );
  if ( !masks.isArray() || masks.empty() )
    return out;
  out.present = true;
  const Json::Int bandCount = understanding.get( "band_count", 0 ).asInt();
  for ( const Json::Value &entry : masks )
  {
    if ( out.count >= FactsLimits::kMaxMasks )
    {
      out.truncated = true;
      break;
    }
    if ( !entry.isObject() )
      continue;
    const std::string role = entry.get( "role", "" ).asString();
    if ( role.empty() || !isMaskRoleName( role ) )
      continue;
    ++out.count;
    out.roles.push_back( lowered( role ) );
    if ( entry.isMember( "band" ) && entry["band"].isNumeric() )
    {
      const int band = entry["band"].asInt();
      out.bands.push_back( band );
      if ( bandCount > 0 && ( band < 0 || band >= bandCount ) )
        out.bandOutOfRange = true;
    }
  }
  out.present = out.count > 0;
  return out;
}

Json::Value QualityMaskFacts::toJson() const
{
  Json::Value json( Json::objectValue );
  json["present"] = present;
  json["count"] = count;
  json["truncated"] = truncated;
  json["band_out_of_range"] = bandOutOfRange;
  Json::Value rolesJson( Json::arrayValue );
  for ( const std::string &role : roles )
    rolesJson.append( role );
  json["roles"] = rolesJson;
  Json::Value bandsJson( Json::arrayValue );
  for ( const int band : bands )
    bandsJson.append( band );
  json["bands"] = bandsJson;

  Json::Value factStatus( Json::objectValue );
  factStatus["present"] = "observed";
  factStatus["roles"] = count > 0 ? "observed" : "unknown";
  factStatus["bands"] = bands.empty() ? "unknown" : "observed";
  json["fact_status"] = factStatus;
  return json;
}

// ---------------------------------------------------------------------------
// Product generation
// ---------------------------------------------------------------------------

ProductGenerationFacts productGenerationFacts( const Json::Value &understanding )
{
  ProductGenerationFacts out;
  Json::Value level = understanding.get( "processing_level", Json::Value() );
  if ( !level.isString() || level.asString().empty() )
    level = understanding.get( "SICNU_PROCESSING_LEVEL", Json::Value() );
  if ( level.isString() && !level.asString().empty() )
  {
    out.processingLevel = level.asString();
    Json::Value type = understanding.get( "product_type", Json::Value() );
    if ( !type.isString() || type.asString().empty() )
      type = understanding.get( "SICNU_PRODUCT_TYPE", Json::Value() );
    if ( type.isString() )
      out.productType = type.asString();

    // Parse WITHOUT radiometric semantics: optional "processing"/"level"
    // words, optional "L"/"Level" prefix, digits = generation level, trailing
    // letters = suffix.
    static const QRegularExpression pattern(
      QStringLiteral( "^\\s*(?:process(?:ing)?[\\s_-]*)?(?:l(?:evel)?)?[\\s_-]*(\\d)\\s*([a-z]*)\\s*$" ),
      QRegularExpression::CaseInsensitiveOption );
    QRegularExpressionMatch match = pattern.match( QString::fromStdString( out.processingLevel ) );
    if ( match.hasMatch() )
    {
      out.generationLevel = match.captured( 1 ).toInt();
      out.levelSuffix = match.captured( 2 ).toStdString();
    }
  }
  return out;
}

Json::Value ProductGenerationFacts::toJson() const
{
  Json::Value json( Json::objectValue );
  const bool observed = !processingLevel.empty();
  if ( observed )
  {
    json["product_type"] = productType.empty() ? Json::Value() : productType;
    json["processing_level"] = processingLevel;
    json["generation_level"] = generationLevel;
    json["level_suffix"] = levelSuffix.empty() ? Json::Value() : levelSuffix;
  }
  Json::Value factStatus( Json::objectValue );
  factStatus["processing_level"] = observed ? "observed" : "unknown";
  factStatus["product_type"] = productType.empty() ? "unknown" : "observed";
  factStatus["generation_level"] = generationLevel >= 0 ? "derived" : "unknown";
  factStatus["level_suffix"] = levelSuffix.empty() ? "unknown" : "derived";
  json["fact_status"] = factStatus;
  return json;
}

// ---------------------------------------------------------------------------
// Model task
// ---------------------------------------------------------------------------

namespace model_task {
bool isKnownModelTask( const std::string &value )
{
  return value == kSegmentation || value == kClassification || value == kDetection ||
         value == kChangeDetection || value == kRegression || value == kEmbedding ||
         value == kExtraction || value == kOther;
}
} // namespace model_task

ModelTaskFacts modelTaskFacts( const Json::Value &modelContract )
{
  ModelTaskFacts out;
  if ( !modelContract.isObject() )
    return out;
  const Json::Value task = modelContract.get( "task", Json::Value() );
  if ( task.isString() && !task.asString().empty() )
  {
    out.rawTask = task.asString();
    std::string normalized = lowered( out.rawTask );
    std::replace( normalized.begin(), normalized.end(), ' ', '_' );
    std::replace( normalized.begin(), normalized.end(), '-', '_' );
    // Family resolution: exact closed match first, then longest containing
    // family (change_detection before detection), then "other".
    out.taskFamily = model_task::kOther;
    if ( model_task::isKnownModelTask( normalized ) && normalized != model_task::kOther )
    {
      out.taskFamily = normalized;
    }
    else
    {
      static const char *kFamilies[] = {
        model_task::kChangeDetection, model_task::kSegmentation, model_task::kClassification,
        model_task::kDetection,       model_task::kRegression,   model_task::kEmbedding,
        model_task::kExtraction,
      };
      for ( const char *family : kFamilies )
      {
        if ( normalized.find( family ) != std::string::npos )
        {
          out.taskFamily = family;
          break;
        }
      }
    }
  }
  const Json::Value readiness = modelContract.get( "readiness", Json::Value() );
  if ( readiness.isString() )
    out.readiness = readiness.asString();
  const Json::Value compatible = modelContract.get( "compatible", Json::Value() );
  if ( compatible.isBool() )
  {
    out.compatibleDeclared = true;
    out.compatible = compatible.asBool();
  }
  const Json::Value cost =
    modelContract.isMember( "estimated_cost" ) ? modelContract["estimated_cost"]
                                               : modelContract.get( "cost", Json::Value() );
  if ( cost.isObject() )
  {
    if ( cost.isMember( "estimated_ram_mb" ) && cost["estimated_ram_mb"].isNumeric() )
      out.estimatedRamMb = cost["estimated_ram_mb"].asInt64();
    if ( cost.isMember( "gpu_accelerated" ) && cost["gpu_accelerated"].isBool() )
      out.gpu = cost["gpu_accelerated"].asBool();
  }
  return out;
}

Json::Value ModelTaskFacts::toJson() const
{
  Json::Value json( Json::objectValue );
  const bool taskObserved = !rawTask.empty();
  if ( taskObserved )
  {
    json["raw_task"] = rawTask;
    json["task_family"] = taskFamily;
  }
  json["task_family_effective"] = taskObserved ? taskFamily : model_task::kOther;
  if ( !readiness.empty() )
    json["readiness"] = readiness;
  if ( compatibleDeclared )
    json["compatible"] = compatible;
  if ( estimatedRamMb > 0 )
    json["estimated_ram_mb"] = static_cast<Json::Int64>( estimatedRamMb );
  json["gpu"] = gpu;

  Json::Value factStatus( Json::objectValue );
  factStatus["raw_task"] = taskObserved ? "observed" : "unknown";
  factStatus["task_family"] = taskObserved ? "derived" : "unknown";
  factStatus["task_family_effective"] = taskObserved ? "derived" : "assumed";
  factStatus["readiness"] = readiness.empty() ? "unknown" : "observed";
  factStatus["compatible"] = compatibleDeclared ? "observed" : "unknown";
  factStatus["estimated_ram_mb"] = estimatedRamMb > 0 ? "observed" : "unknown";
  factStatus["gpu"] = "observed";
  json["fact_status"] = factStatus;
  return json;
}

// ---------------------------------------------------------------------------
// Resource facts
// ---------------------------------------------------------------------------

ResourceFacts resourceFacts( long long nodeEstimateMb, const Json::Value &capabilityCost,
                             const Json::Value &expectations, const std::string &device )
{
  ResourceFacts out;
  out.nodeEstimateMb = nodeEstimateMb > 0 ? nodeEstimateMb : 0;
  if ( capabilityCost.isObject() && capabilityCost.isMember( "estimated_ram_mb" ) &&
       capabilityCost["estimated_ram_mb"].isNumeric() )
    out.capabilityDemandMb = capabilityCost["estimated_ram_mb"].asInt64();
  if ( expectations.isObject() && expectations.isMember( "max_ram_mb" ) &&
       expectations["max_ram_mb"].isNumeric() )
    out.expectationsMaxRamMb = expectations["max_ram_mb"].asInt64();
  out.device = device == "cpu" || device == "gpu" ? device : std::string();
  out.budgetKnown = out.capabilityDemandMb > 0 && out.expectationsMaxRamMb > 0;
  out.overBudget = out.budgetKnown && out.capabilityDemandMb > out.expectationsMaxRamMb;
  return out;
}

Json::Value ResourceFacts::toJson() const
{
  Json::Value json( Json::objectValue );
  if ( nodeEstimateMb > 0 )
    json["node_estimate_mb"] = static_cast<Json::Int64>( nodeEstimateMb );
  if ( capabilityDemandMb > 0 )
    json["capability_demand_mb"] = static_cast<Json::Int64>( capabilityDemandMb );
  if ( expectationsMaxRamMb > 0 )
    json["expectations_max_ram_mb"] = static_cast<Json::Int64>( expectationsMaxRamMb );
  if ( !device.empty() )
    json["device"] = device;
  if ( budgetKnown )
    json["over_budget"] = overBudget;

  Json::Value factStatus( Json::objectValue );
  factStatus["node_estimate_mb"] = nodeEstimateMb > 0 ? "declared" : "unknown";
  factStatus["capability_demand_mb"] = capabilityDemandMb > 0 ? "derived" : "unknown";
  factStatus["expectations_max_ram_mb"] = expectationsMaxRamMb > 0 ? "declared" : "unknown";
  factStatus["device"] = device.empty() ? "unknown" : "declared";
  factStatus["over_budget"] = budgetKnown ? "derived" : "unknown";
  json["fact_status"] = factStatus;
  return json;
}

// ---------------------------------------------------------------------------
// Facts digest
// ---------------------------------------------------------------------------

std::string workflowFactsDigest( const Json::Value &facts )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  const std::string serialized = Json::writeString( builder, facts );
  const QByteArray digest = QCryptographicHash::hash(
    QByteArray::fromStdString( serialized ), QCryptographicHash::Sha256 );
  return QString::fromLatin1( digest.left( 16 ).toHex() ).toStdString();
}

} // namespace sicnu::agent::harness::wfacts
