// Authoritative algorithm-search engine (capability search track).
// Contract: docs live in algorithm_search.h and
// .planning/ds41-capability-search-13/DECISIONS.md (D3-D6).

#include "processing/framework/algorithm_search.h"

#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <set>

namespace sicnu::processing {

namespace {

/// NFKD + strip non-spacing marks + case-fold: deterministic, locale-free,
/// accent-insensitive comparison basis for every haystack/needle.
QString foldSearchText( const std::string &text )
{
  QString folded = QString::fromStdString( text ).normalized( QString::NormalizationForm_KD );
  QString out;
  out.reserve( folded.size() );
  for ( const QChar &ch : folded )
  {
    const QChar::Category cat = ch.category();
    if ( cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining
         || cat == QChar::Mark_Enclosing )
      continue;
    out.append( ch );
  }
  return out.toCaseFolded();
}

bool isTokenChar( const QChar &ch )
{
  return ch.isLetterOrNumber() || ch == QLatin1Char( '_' ) || ch == QLatin1Char( ':' );
}

std::vector<QString> tokenize( const QString &folded )
{
  std::vector<QString> tokens;
  QString current;
  for ( const QChar &ch : folded )
  {
    if ( isTokenChar( ch ) )
      current.append( ch );
    else if ( !current.isEmpty() )
    {
      tokens.push_back( current );
      current.clear();
    }
  }
  if ( !current.isEmpty() )
    tokens.push_back( current );
  return tokens;
}

/// Per-token field weights: id 8, tags 4, displayName 4, group 2,
/// purpose 2, description 1. A token matching a field adds that weight
/// once (field max, not per occurrence).
struct FoldedDescriptor
{
  QString id;
  QString displayName;
  QString group;
  QString description;
  QString purpose;
  QString taskFamily;
  QStringList tags;
  QStringList modalities;
};

FoldedDescriptor foldDescriptor( const AlgorithmDescriptor &desc,
                                 const std::vector<std::string> &modalities )
{
  FoldedDescriptor f;
  f.id = foldSearchText( desc.id );
  f.displayName = foldSearchText( desc.displayName );
  f.group = foldSearchText( desc.group );
  f.description = foldSearchText( desc.description );
  f.purpose = foldSearchText( desc.agentMetadata.purpose );
  f.taskFamily = foldSearchText( desc.agentMetadata.taskFamily );
  for ( const auto &tag : desc.agentMetadata.tags )
    f.tags.push_back( foldSearchText( tag ) );
  for ( const auto &m : modalities )
    f.modalities.push_back( foldSearchText( m ) );
  return f;
}

int tokenScore( const FoldedDescriptor &f, const QString &token )
{
  int score = 0;
  if ( f.id.contains( token ) )
    score += 8;
  for ( const QString &tag : f.tags )
    if ( tag.contains( token ) )
    {
      score += 4;
      break;
    }
  if ( f.displayName.contains( token ) )
    score += 4;
  if ( f.group.contains( token ) )
    score += 2;
  if ( f.purpose.contains( token ) )
    score += 2;
  if ( f.description.contains( token ) )
    score += 1;
  return score;
}

bool anyExactFolded( const QStringList &declared, const QString &wanted )
{
  for ( const QString &d : declared )
    if ( d == wanted )
      return true;
  return false;
}

/// Levenshtein distance with an early-exit ceiling — returns maxDist+1 once
/// the distance provably exceeds the bound (bounded work per pair).
int boundedEditDistance( const QString &a, const QString &b, int maxDist )
{
  const int n = a.size();
  const int m = b.size();
  if ( std::abs( n - m ) > maxDist )
    return maxDist + 1;
  std::vector<int> prev( m + 1 ), cur( m + 1 );
  for ( int j = 0; j <= m; ++j )
    prev[j] = j;
  for ( int i = 1; i <= n; ++i )
  {
    cur[0] = i;
    int rowMin = cur[0];
    for ( int j = 1; j <= m; ++j )
    {
      const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
      cur[j] = std::min( { prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost } );
      rowMin = std::min( rowMin, cur[j] );
    }
    if ( rowMin > maxDist )
      return maxDist + 1;
    std::swap( prev, cur );
  }
  return prev[m];
}

} // namespace

std::vector<std::string> splitSearchList( const std::string &csv )
{
  std::vector<std::string> out;
  std::string current;
  for ( const char c : csv )
  {
    if ( c == ',' )
    {
      const QString trimmed = QString::fromStdString( current ).trimmed();
      if ( !trimmed.isEmpty() )
        out.push_back( trimmed.toStdString() );
      current.clear();
    }
    else
      current.push_back( c );
  }
  const QString trimmed = QString::fromStdString( current ).trimmed();
  if ( !trimmed.isEmpty() )
    out.push_back( trimmed.toStdString() );
  return out;
}

std::vector<std::string> declaredModalities( const AlgorithmDescriptor &descriptor )
{
  std::set<std::string> out;
  // Tolerate string-or-array contract values (the capability catalog reader
  // accepts both shapes); non-string members are ignored rather than
  // throwing Json::LogicError into every search.
  const auto insertStrings = [&out]( const Json::Value &value ) {
    if ( value.isString() )
    {
      if ( !value.asString().empty() )
        out.insert( value.asString() );
    }
    else if ( value.isArray() )
    {
      for ( const auto &element : value )
        if ( element.isString() && !element.asString().empty() )
          out.insert( element.asString() );
    }
  };
  for ( const auto &port : descriptor.inputs )
  {
    if ( !port.rsContract.isObject() )
      continue;
    insertStrings( port.rsContract["modality"] );
    insertStrings( port.rsContract["modalities"] );
    insertStrings( port.rsContract["dataKind"] );
  }
  return { out.begin(), out.end() };
}

std::string AlgorithmSearchQuery::validationError() const
{
  if ( text.size() > kMaxTextLength )
    return "query text exceeds " + std::to_string( kMaxTextLength ) + " bytes";
  for ( const std::string *scalar : { &group, &purpose, &taskFamily, &inputType, &outputType } )
    if ( scalar->size() > kMaxFilterLength )
      return "filter value exceeds " + std::to_string( kMaxFilterLength ) + " bytes";
  for ( const std::vector<std::string> *list : { &tags, &modalities } )
  {
    if ( list->size() > kMaxListValues )
      return "filter list exceeds " + std::to_string( kMaxListValues ) + " values";
    for ( const auto &v : *list )
      if ( v.size() > kMaxFilterLength )
        return "filter value exceeds " + std::to_string( kMaxFilterLength ) + " bytes";
  }
  return {};
}

AlgorithmSearchResult searchAlgorithms( const std::vector<AlgorithmDescriptor> &universe,
                                        const AlgorithmSearchQuery &query )
{
  AlgorithmSearchResult result;

  const std::string refusal = query.validationError();
  if ( !refusal.empty() )
  {
    result.error = refusal;
    return result;
  }

  // --- fold the query side once ------------------------------------------
  const QString foldedText = foldSearchText( query.text );
  const std::vector<QString> tokens = tokenize( foldedText );
  const QString foldedGroup = foldSearchText( query.group );
  const QString foldedPurpose = foldSearchText( query.purpose );
  const QString foldedTask = foldSearchText( query.taskFamily );
  const QString foldedInputType = foldSearchText( query.inputType );
  const QString foldedOutputType = foldSearchText( query.outputType );

  QStringList wantedTags;
  for ( const auto &t : query.tags )
    wantedTags.push_back( foldSearchText( t ) );
  QStringList wantedModalities;
  for ( const auto &m : query.modalities )
    wantedModalities.push_back( foldSearchText( m ) );

  // --- match + score ------------------------------------------------------
  std::vector<AlgorithmSearchHit> matched;
  matched.reserve( universe.size() );

  std::set<std::string> groups, tags, tasks, modalities, dataTypes;
  std::vector<QString> foldedIds;
  foldedIds.reserve( universe.size() );

  for ( size_t i = 0; i < universe.size(); ++i )
  {
    const AlgorithmDescriptor &desc = universe[i];
    const std::vector<std::string> declaredMods = declaredModalities( desc );
    const FoldedDescriptor f = foldDescriptor( desc, declaredMods );
    foldedIds.push_back( f.id );

    // vocabulary (computed over the whole universe — honest filter space)
    if ( !desc.group.empty() )
      groups.insert( desc.group );
    for ( const auto &t : desc.agentMetadata.tags )
      if ( !t.empty() )
        tags.insert( t );
    if ( !desc.agentMetadata.taskFamily.empty() )
      tasks.insert( desc.agentMetadata.taskFamily );
    for ( const auto &m : declaredMods )
      modalities.insert( m );
    for ( const auto &port : desc.inputs )
      if ( port.type != DataType::Any )
        dataTypes.insert( dataTypeToString( port.type ) );
    for ( const auto &port : desc.outputs )
      if ( port.type != DataType::Any )
        dataTypes.insert( dataTypeToString( port.type ) );

    // --- structured filters (AND across dimensions) ---
    if ( !foldedGroup.isEmpty() && f.group != foldedGroup )
      continue;
    if ( !wantedTags.isEmpty() )
    {
      bool tagHit = false;
      for ( const QString &wanted : wantedTags )
        if ( anyExactFolded( f.tags, wanted ) )
        {
          tagHit = true;
          break;
        }
      if ( !tagHit )
        continue;
    }
    if ( !foldedPurpose.isEmpty() && !f.purpose.contains( foldedPurpose ) )
      continue;
    if ( !foldedTask.isEmpty() && f.taskFamily != foldedTask )
      continue;
    if ( !wantedModalities.isEmpty() )
    {
      bool modHit = false;
      for ( const QString &wanted : wantedModalities )
        if ( anyExactFolded( f.modalities, wanted ) )
        {
          modHit = true;
          break;
        }
      if ( !modHit )
        continue;
    }
    if ( !foldedInputType.isEmpty() )
    {
      bool typeHit = false;
      for ( const auto &port : desc.inputs )
        if ( foldSearchText( dataTypeToString( port.type ) ) == foldedInputType )
        {
          typeHit = true;
          break;
        }
      if ( !typeHit )
        continue;
    }
    if ( !foldedOutputType.isEmpty() )
    {
      bool typeHit = false;
      for ( const auto &port : desc.outputs )
        if ( foldSearchText( dataTypeToString( port.type ) ) == foldedOutputType )
        {
          typeHit = true;
          break;
        }
      if ( !typeHit )
        continue;
    }
    if ( query.largeRasterSafeOnly && !desc.agentMetadata.largeRasterSafe )
      continue;

    // --- free text: AND of tokens; whole-query == id pins to top ---
    int score = 0;
    bool textOk = true;
    for ( const QString &token : tokens )
    {
      const int s = tokenScore( f, token );
      if ( s == 0 )
      {
        textOk = false;
        break;
      }
      score += s;
    }
    if ( !textOk )
      continue;
    if ( !foldedText.isEmpty() && f.id == foldedText )
      score += 100;

    matched.push_back( { i, score } );
  }

  // --- deterministic total order: score desc, id asc ---------------------
  std::sort( matched.begin(), matched.end(), [&]( const AlgorithmSearchHit &a,
                                                  const AlgorithmSearchHit &b ) {
    if ( a.score != b.score )
      return a.score > b.score;
    if ( foldedIds[a.index] != foldedIds[b.index] )
      return foldedIds[a.index] < foldedIds[b.index];
    return a.index < b.index;
  } );

  // --- pagination ---------------------------------------------------------
  const int limit = query.limit <= 0 ? AlgorithmSearchQuery::kDefaultLimit
                                     : std::min( query.limit, AlgorithmSearchQuery::kMaxLimit );
  const int cursor = std::max( 0, query.cursor );
  result.total = static_cast<int>( matched.size() );
  result.limit = limit;
  result.cursor = cursor;
  const int start = std::min( cursor, result.total );
  const int end = std::min( result.total, start + limit );
  result.hits.assign( matched.begin() + start, matched.begin() + end );
  result.nextCursor = end < result.total ? end : -1;

  // --- vocabulary + zero-hit suggestions ---------------------------------
  result.vocabulary.groups.assign( groups.begin(), groups.end() );
  result.vocabulary.tags.assign( tags.begin(), tags.end() );
  result.vocabulary.taskFamilies.assign( tasks.begin(), tasks.end() );
  result.vocabulary.modalities.assign( modalities.begin(), modalities.end() );
  result.vocabulary.dataTypes.assign( dataTypes.begin(), dataTypes.end() );

  if ( matched.empty() && !tokens.empty() )
  {
    // Closest ids by bounded edit distance between a token and each id
    // segment (rs:sar_speckle -> [rs, sar, speckle]): catches typos and
    // truncations without a ranking subsystem. Distance ≤ 2, tokens and
    // segments ≥ 4 chars; best 5 by (distance asc, id asc).
    static const QRegularExpression segmenter( QStringLiteral( "[^a-z0-9]+" ) );
    std::vector<std::pair<int, std::string>> ranked;
    for ( size_t i = 0; i < universe.size(); ++i )
    {
      const QStringList segments = foldedIds[i].split( segmenter, Qt::SkipEmptyParts );
      int best = 3;
      for ( const QString &token : tokens )
      {
        if ( token.size() < 4 )
          continue;
        for ( const QString &segment : segments )
        {
          if ( segment.size() < 4 )
            continue;
          best = std::min( best, boundedEditDistance( token, segment, best - 1 ) );
          if ( best == 1 )
            break;
        }
      }
      if ( best <= 2 )
        ranked.emplace_back( best, universe[i].id );
    }
    std::sort( ranked.begin(), ranked.end(), []( const auto &a, const auto &b ) {
      if ( a.first != b.first )
        return a.first < b.first;
      return a.second < b.second;
    } );
    const size_t cap = std::min<size_t>( 5, ranked.size() );
    for ( size_t i = 0; i < cap; ++i )
      result.suggestions.push_back( ranked[i].second );
  }

  return result;
}

} // namespace sicnu::processing
