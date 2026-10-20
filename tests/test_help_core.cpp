/***************************************************************************
  tests/test_help_core.cpp
  Unified Help System 6.0 — core knowledge-layer suite.
  Covers: Help ID grammar, registry integrity (duplicates/aliases/references),
  search determinism + bounds, content store validation, provider composition,
  availability-fact presentation, compact agent summaries, Markdown writer,
  and the embedded shipped-content health check.
 ***************************************************************************/

#include "help/help_id.h"
#include "help/help_descriptor.h"
#include "help/help_registry.h"
#include "help/help_search_index.h"
#include "help/help_content_store.h"
#include "help/help_catalog_source.h"
#include "help/command_help_provider.h"
#include "help/operator_help_provider.h"
#include "help/help_composition.h"
#include "help/help_presenter.h"
#include "help/help_markdown_writer.h"
#include "help/diagnostic_catalog.h"
#include "help/availability_facts.h"
#include "help/terminology_provider.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector>

using namespace sicnu::help;

namespace
{

/// Stub command source for provider tests.
class StubCommandSource : public CommandCatalogSource
{
  public:
    QVector<CommandFact> commands() const override
    {
        CommandFact save;
        save.id = QStringLiteral( "layer.saveEdits" );
        save.title = QStringLiteral( "保存编辑" );
        save.description = QStringLiteral( "保存矢量编辑。" );
        save.category = QStringLiteral( "矢量编辑" );
        save.keywords << QStringLiteral( "保存" );
        return { save };
    }
};

/// Stub operator source carrying an authoritative-schema-shaped JSON.
class StubOperatorSource : public OperatorCatalogSource
{
  public:
    QVector<OperatorFact> operators() const override
    {
        OperatorFact op;
        op.id = QStringLiteral( "rs:stub_speckle" );
        op.displayName = QStringLiteral( "Stub Speckle" );
        op.group = QStringLiteral( "SAR" );
        op.description = QStringLiteral( "stub operator for provider tests" );
        op.determinismGrade = QStringLiteral( "bit_exact" );
        op.memoryPolicy = QStringLiteral( "streaming" );

        Json::Value props( Json::objectValue );
        Json::Value kernel( Json::objectValue );
        kernel["name"] = "kernelSize";
        kernel["type"] = "integer";
        kernel["description"] = "Odd filter window size";
        kernel["default"] = 3;
        kernel["minimum"] = 3;
        kernel["maximum"] = 15;
        props["kernelSize"] = kernel;
        Json::Value method( Json::objectValue );
        method["name"] = "method";
        method["type"] = "string";
        method["description"] = "Filter kernel";
        method["enum"] = Json::Value( Json::arrayValue );
        method["enum"].append( "lee" );
        method["enum"].append( "frost" );
        method["default"] = "lee";
        props["method"] = method;
        Json::Value input( Json::objectValue );
        input["name"] = "input";
        input["type"] = "string";
        input["description"] = "Input raster";
        input["format"] = "raster";
        input["required"] = true;
        props["input"] = input;

        Json::Value schema( Json::objectValue );
        schema["properties"] = props;
        schema["required"] = Json::Value( Json::arrayValue );
        schema["required"].append( "input" );
        schema["required"].append( "output" );
        op.schema = schema;
        return { op };
    }
};

} // namespace

TEST_CASE( "Help ID grammar", "[help][id]" )
{
  CHECK( HelpId::isValid( QStringLiteral( "command.layer.toggleEditing" ) ) );
  CHECK( HelpId::isValid( QStringLiteral( "operator.rs.sar_speckle" ) ) );
  CHECK( HelpId::isValid( QStringLiteral( "parameter.rs.sar_speckle.kernelSize" ) ) );
  CHECK( HelpId::isValid( QStringLiteral( "diagnostic.harness.dataset_not_found" ) ) );
  CHECK( HelpId::isValid( QStringLiteral( "concept.dataset.spatial_leakage" ) ) );
  CHECK_FALSE( HelpId::isValid( QStringLiteral( "command" ) ) );
  CHECK_FALSE( HelpId::isValid( QStringLiteral( "bogus.kind.of.thing" ) ) );
  CHECK_FALSE( HelpId::isValid( QStringLiteral( "command.rs.band-math" ) ) );
  CHECK_FALSE( HelpId::isValid( QStringLiteral( "command.rs..empty" ) ) );
  CHECK_FALSE( HelpId::isValid( QString() ) );

  REQUIRE( HelpId::kindOf( QStringLiteral( "parameter.a.b" ) ) == HelpKind::Parameter );
  REQUIRE( HelpId::kindOf( QStringLiteral( "workbench.classification" ) ) == HelpKind::Workbench );

  CHECK( HelpId::parameterId( QStringLiteral( "rs:sar_speckle" ), QStringLiteral( "kernelSize" ) )
         == QStringLiteral( "parameter.rs.sar_speckle.kernelSize" ) );
  CHECK( HelpId::domainForOperatorId( QStringLiteral( "rs:sar_speckle" ) )
         == QStringLiteral( "rs.sar_speckle" ) );
  CHECK( HelpId::diagnosticId( DiagnosticFamily::Harness, QStringLiteral( "DATASET_NOT_FOUND" ) )
         == QStringLiteral( "diagnostic.harness.dataset_not_found" ) );
  CHECK( HelpId::diagnosticId( DiagnosticFamily::Dataset, QStringLiteral( "label.unknown_class" ) )
         == QStringLiteral( "diagnostic.dataset.label.unknown_class" ) );
  CHECK( HelpId::normalizeCode( QStringLiteral( "CamelCaseCode" ) )
         == QStringLiteral( "camel_case_code" ) );
}

TEST_CASE( "Registry integrity: duplicates, aliases, references", "[help][registry]" )
{
  HelpRegistry registry;

  HelpDescriptor d;
  d.id = QStringLiteral( "command.layer.saveEdits" );
  d.kind = HelpKind::Command;
  d.title = QStringLiteral( "保存编辑" );
  d.summary = QStringLiteral( "保存矢量编辑。" );
  d.command = CommandHelp{};

  QString error;
  REQUIRE( registry.registerDescriptor( d, &error ) );

  // duplicate rejected
  CHECK_FALSE( registry.registerDescriptor( d, &error ) );
  CHECK( error.contains( QStringLiteral( "duplicate" ) ) );

  // kind mismatch rejected
  HelpDescriptor wrong = d;
  wrong.id = QStringLiteral( "operator.rs.saveEdits" );
  CHECK_FALSE( registry.registerDescriptor( wrong, &error ) );
  CHECK( error.contains( QStringLiteral( "kind mismatch" ) ) );

  // alias resolution (one hop)
  REQUIRE( registry.registerAlias( QStringLiteral( "command.layer.save" ),
                                   QStringLiteral( "command.layer.saveEdits" ), &error ) );
  const HelpDescriptor *aliased = registry.find( QStringLiteral( "command.layer.save" ) );
  REQUIRE( aliased );
  CHECK( aliased->id == QStringLiteral( "command.layer.saveEdits" ) );

  // dangling reference is reported by validateReferences
  HelpDescriptor withRef = d;
  withRef.id = QStringLiteral( "concept.test.dangling" );
  withRef.kind = HelpKind::Concept;
  withRef.relatedIds << QStringLiteral( "concept.does.not_exist" );
  REQUIRE( registry.registerDescriptor( withRef, &error ) );
  const QStringList problems = registry.validateReferences();
  REQUIRE( problems.size() == 1 );
  CHECK( problems.first().contains( QStringLiteral( "concept.does.not_exist" ) ) );
}

TEST_CASE( "Search index: deterministic, bilingual, bounded", "[help][search]" )
{
  HelpRegistry registry;
  const char *titles[] = { "SAR 斑点滤波", "Band Math", "NDVI 计算", "水体指数 MNDWI" };
  for ( int i = 0; i < 4; ++i ) {
    HelpDescriptor d;
    d.id = QStringLiteral( "operator.rs.topic%1" ).arg( i );
    d.kind = HelpKind::Operator;
    d.title = QString::fromUtf8( titles[i] );
    d.summary = QStringLiteral( "topic %1 summary" ).arg( i );
    d.keywords << QStringLiteral( "keyword%1" ).arg( i );
    REQUIRE( registry.registerDescriptor( d ) );
  }

  HelpSearchIndex index;
  index.build( registry.all() );
  REQUIRE( index.isValid() );
  CHECK( index.topicCount() == 4 );

  // deterministic: two identical queries give identical result orders
  const QVector<SearchHit> first = index.search( QStringLiteral( "斑点" ), 20 );
  const QVector<SearchHit> second = index.search( QStringLiteral( "斑点" ), 20 );
  REQUIRE( first.size() == second.size() );
  for ( int i = 0; i < first.size(); ++i )
    CHECK( first[i].id == second[i].id );
  REQUIRE_FALSE( first.isEmpty() );
  CHECK( first.first().id == QStringLiteral( "operator.rs.topic0" ) );

  // id query resolves directly
  const QVector<SearchHit> byId = index.search( QStringLiteral( "operator.rs.topic2" ), 20 );
  REQUIRE_FALSE( byId.isEmpty() );
  CHECK( byId.first().id == QStringLiteral( "operator.rs.topic2" ) );

  // result cap honored
  const QVector<SearchHit> capped = index.search( QStringLiteral( "topic" ), 2 );
  CHECK( capped.size() == 2 );

  // scale bounds: 2000 topics must index in < 500 ms and query in < 5 ms avg
  HelpRegistry big;
  for ( int i = 0; i < 2000; ++i ) {
    HelpDescriptor d;
    d.id = QStringLiteral( "operator.rs.scale_op_%1" ).arg( i );
    d.kind = HelpKind::Operator;
    d.title = QStringLiteral( "算子 %1" ).arg( i );
    d.summary = QStringLiteral( "scalable operator %1 for index benchmark" ).arg( i );
    d.keywords << QStringLiteral( "kw%1" ).arg( i % 50 );
    REQUIRE( big.registerDescriptor( d ) );
  }
  HelpSearchIndex bigIndex;
  QElapsedTimer timer;
  timer.start();
  bigIndex.build( big.all() );
  const qint64 buildMs = timer.elapsed();
  CHECK( buildMs < 500 );

  QVector<qint64> latencies;
  for ( int q = 0; q < 50; ++q ) {
    QElapsedTimer qt;
    qt.start();
    const QVector<SearchHit> hits = bigIndex.search( QStringLiteral( "operator scale %1" ).arg( q ), 20 );
    latencies << qt.elapsed();
    CHECK_FALSE( hits.isEmpty() );
  }
  qint64 total = 0;
  for ( qint64 ms : latencies )
    total += ms;
  const qint64 avgMs = total / latencies.size();
  // Release builds must stay < 5 ms; debug (-Od, iterator checking) gets slack.
#ifdef NDEBUG
  const qint64 budget = 5;
#else
  // MSVC -Od + iterator checking is ~6x slower; keep a sanity bound only.
  const qint64 budget = 40;
#endif
  CHECK( avgMs < budget );
  INFO( "avg search latency ms: " << avgMs );
}

TEST_CASE( "Content store parses and validates JSON knowledge", "[help][content]" )
{
  const HelpContentStore::LoadResult result = HelpContentStore::loadFromResources();
  INFO( "errors: " << result.errors.join( QStringLiteral( "; " ) ).toStdString() );
  CHECK( result.errors.isEmpty() );
  CHECK( result.descriptors > 100 ); // shipped content: commands+operators+diagnostics+concepts+workbenches
}

TEST_CASE( "Content store rejects unknown retry values (fail-closed)", "[help][content]" )
{
  // F20: a typo'd retry sense used to silently degrade to Derived, so the
  // retry-sense drift checks were bypassed for exactly the pages that had a
  // typo. Unknown values must now surface as a load error.
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.path() + QStringLiteral( "/diag.json" );
  {
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( R"json([{
      "id": "diagnostic.harness.sample_failure",
      "family": "harness",
      "code": "SAMPLE_FAILURE",
      "title": "样例失败",
      "whatHappened": "发生了样例错误。",
      "whyItMatters": "会阻断后续步骤。",
      "severity": "error",
      "retry": "retryable",
      "remediation": [ "检查输入" ]
    }])json" );
  }
  const HelpContentStore::LoadResult result = HelpContentStore::loadFromDirectory( dir.path() );
  INFO( "errors: " << result.errors.join( QStringLiteral( "; " ) ).toStdString() );
  REQUIRE_FALSE( result.errors.isEmpty() );
  CHECK( result.errors.first().contains( QStringLiteral( "retry" ) ) );

  // the valid vocabulary still loads cleanly
  {
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    f.write( R"json([{
      "id": "diagnostic.harness.sample_failure",
      "family": "harness",
      "code": "SAMPLE_FAILURE",
      "title": "样例失败",
      "whatHappened": "发生了样例错误。",
      "whyItMatters": "会阻断后续步骤。",
      "severity": "error",
      "retry": "transient",
      "remediation": [ "检查输入" ]
    }])json" );
  }
  const HelpContentStore::LoadResult ok = HelpContentStore::loadFromDirectory( dir.path() );
  INFO( "errors: " << ok.errors.join( QStringLiteral( "; " ) ).toStdString() );
  CHECK( ok.errors.isEmpty() );
}

TEST_CASE( "Provider composition merges registry facts with knowledge", "[help][provider]" )
{
  HelpRegistry knowledge;
  // add curated parameter knowledge matching the stub schema
  HelpDescriptor kp;
  kp.id = HelpId::parameterId( QStringLiteral( "rs:stub_speckle" ), QStringLiteral( "kernelSize" ) );
  kp.kind = HelpKind::Parameter;
  kp.title = QStringLiteral( "kernelSize" );
  ParameterKnowledge pk;
  pk.unit = QStringLiteral( "像素" );
  pk.meaning = QStringLiteral( "局部窗口" );
  kp.parameter = pk;
  REQUIRE( knowledge.registerDescriptor( kp ) );

  // stale knowledge must be reported
  HelpDescriptor stale;
  stale.id = QStringLiteral( "operator.rs.gone_operator" );
  stale.kind = HelpKind::Operator;
  stale.title = QStringLiteral( "gone" );
  REQUIRE( knowledge.registerDescriptor( stale ) );

  HelpRegistry composed;
  QStringList errors;
  OperatorHelpProvider::compose( StubOperatorSource(), knowledge, composed, &errors );
  INFO( "compose errors: " << errors.join( QStringLiteral( " | " ) ).toStdString() );
  CHECK( errors.size() == 1 ); // stale operator knowledge
  CHECK( errors.first().contains( QStringLiteral( "gone_operator" ) ) );

  const HelpDescriptor *op = composed.find( QStringLiteral( "operator.rs.stub_speckle" ) );
  REQUIRE( op );
  CHECK( op->kind == HelpKind::Operator );

  // base tier: every schema parameter got a descriptor
  CHECK( composed.find( QStringLiteral( "parameter.rs.stub_speckle.kernelSize" ) ) );
  CHECK( composed.find( QStringLiteral( "parameter.rs.stub_speckle.method" ) ) );
  CHECK( composed.find( QStringLiteral( "parameter.rs.stub_speckle.input" ) ) );

  // curated knowledge merged on top of schema facts
  ParameterHelpEntry entry = OperatorHelpProvider::parameterHelp(
      StubOperatorSource().operators().first(), QStringLiteral( "kernelSize" ), composed );
  CHECK( entry.knowledge.has_value() );
  CHECK( entry.knowledge->unit == QStringLiteral( "像素" ) );
  CHECK( entry.fact.hasRange );
  CHECK( entry.fact.minimum == 3.0 );
  CHECK( entry.fact.maximum == 15.0 );
  CHECK( entry.fact.defaultText == QStringLiteral( "3" ) );
  CHECK( entry.fact.type == QStringLiteral( "integer" ) );

  CommandHelpProvider::compose( StubCommandSource(), knowledge, composed, &errors );
  const HelpDescriptor *cmd = composed.find( QStringLiteral( "command.layer.saveEdits" ) );
  REQUIRE( cmd );
  CHECK( cmd->title == QStringLiteral( "保存编辑" ) );
  CHECK( cmd->summary == QStringLiteral( "保存矢量编辑。" ) );
}

TEST_CASE( "Availability explanation presentation", "[help][availability]" )
{
  AvailabilityExplanation explanation;
  explanation.commandId = QStringLiteral( "layer.saveEdits" );
  explanation.available = false;
  explanation.facts << AvailabilityFact{ QStringLiteral( "已选中图层" ), QStringLiteral( "layer.selected" ), true };
  explanation.facts << AvailabilityFact{ QStringLiteral( "已选中矢量图层" ), QStringLiteral( "vector.selected" ), true };
  explanation.facts << AvailabilityFact{ QStringLiteral( "编辑会话未开启" ), QStringLiteral( "editing.active" ), false };
  explanation.suggestedCommandTitle = QStringLiteral( "切换编辑" );
  explanation.suggestedCommandId = QStringLiteral( "layer.toggleEditing" );

  const QString text = explanation.toText();
  CHECK( text.contains( QStringLiteral( "不可用" ) ) );
  CHECK( text.contains( u'✓' ) );
  CHECK( text.contains( u'✗' ) );
  CHECK( text.contains( QStringLiteral( "切换编辑" ) ) );

  const QString concise = explanation.toConciseLine();
  CHECK( concise == QStringLiteral( "需要：编辑会话未开启" ) );
}

TEST_CASE( "Presenter layers stay bounded", "[help][presenter]" )
{
  HelpDescriptor d;
  d.id = QStringLiteral( "operator.rs.presenter" );
  d.kind = HelpKind::Operator;
  d.title = QStringLiteral( "测试算子" );
  d.summary = QStringLiteral( "这是一个用于测试的算子摘要。" );
  AlgorithmPage page;
  page.whatItDoes = QStringLiteral( "原理描述" );
  page.whenToUse = QStringLiteral( "适用场景" );
  d.algorithm = page;

  const QString tooltip = HelpPresenter::tooltip( d, 90 );
  CHECK( tooltip.size() <= 90 );
  CHECK( tooltip.contains( QStringLiteral( "测试算子" ) ) );

  const QString whatsThis = HelpPresenter::whatsThis( d );
  CHECK( whatsThis.contains( QStringLiteral( "原理" ) ) );
  CHECK( whatsThis.contains( QStringLiteral( "F1" ) ) );
}

TEST_CASE( "Compact agent summaries respect the token budget", "[help][compact]" )
{
  HelpDescriptor d;
  d.id = QStringLiteral( "operator.rs.compact_check" );
  d.kind = HelpKind::Operator;
  d.title = QStringLiteral( "压缩测试" );
  d.summary = QStringLiteral( "summary text that should be truncated under a tiny budget" );
  AlgorithmPage page;
  page.keyParameters << QStringLiteral( "a" ) << QStringLiteral( "b" );
  d.algorithm = page;

  const QString short_ = HelpCompact::summary( d, 40 );
  CHECK( short_.size() <= 40 );
  CHECK( short_.startsWith( QStringLiteral( "operator.rs.compact_check" ) ) );

  const QString json = HelpCompact::toJsonText( d, 60 );
  CHECK( json.size() <= 60 );
  CHECK( json.contains( QStringLiteral( "\"id\"" ) ) );
}

TEST_CASE( "Diagnostic catalog resolves every family and preserves codes", "[help][diagnostics]" )
{
  // compose the shipped content so assertions exercise curated descriptors,
  // not the generated fallback (which would satisfy them vacuously)
  if ( globalHelpRegistry().count() == 0 )
    composeHelpSystem( globalHelpRegistry(), nullptr, nullptr );
  REQUIRE( globalHelpRegistry().count() > 0 );
  const HelpRegistry &registry = globalHelpRegistry();
  DiagnosticCatalog catalog( registry );

  // shipped catalog must resolve a known harness code
  const HelpDescriptor resolved = catalog.resolve( DiagnosticFamily::Harness,
                                                   QStringLiteral( "DATASET_NOT_FOUND" ) );
  CHECK( resolved.diagnostic.has_value() );
  CHECK( resolved.diagnostic->originCode == QStringLiteral( "DATASET_NOT_FOUND" ) );
  CHECK_FALSE( resolved.diagnostic->remediation.isEmpty() );
  // must be the curated page, not the generated fallback
  CHECK( registry.find( resolved.id ) != nullptr );

  // unknown codes fall back but keep the original code byte-identical
  const HelpDescriptor fallback = catalog.resolve( DiagnosticFamily::Operator,
                                                   QStringLiteral( "NotARealCode" ) );
  REQUIRE( fallback.diagnostic.has_value() );
  CHECK( fallback.diagnostic->originCode == QStringLiteral( "NotARealCode" ) );
  CHECK_FALSE( fallback.diagnostic->remediation.isEmpty() );

  // dotted dataset codes keep their dots
  const HelpDescriptor dataset = catalog.resolve( DiagnosticFamily::Dataset,
                                                  QStringLiteral( "label.unknown_class" ) );
  REQUIRE( dataset.diagnostic.has_value() );
  CHECK( dataset.id == QStringLiteral( "diagnostic.dataset.label.unknown_class" ) );
}

TEST_CASE( "helpid links keep id case through QUrl (path, not host)", "[help][presenter]" )
{
  // QUrl lowercases the HOST (RFC 3986); ids must travel in the path.
  const QUrl hostUrl( QStringLiteral( "helpid://command.layer.toggleEditing" ) );
  CHECK( hostUrl.host() == QStringLiteral( "command.layer.toggleediting" ) ); // documents the hazard
  const QUrl pathUrl( QStringLiteral( "helpid:/command.layer.toggleEditing" ) );
  QString id = pathUrl.path();
  while ( id.startsWith( u'/' ) )
    id.remove( 0, 1 );
  CHECK( id == QStringLiteral( "command.layer.toggleEditing" ) );
}

TEST_CASE( "Markdown writer output is stable and complete", "[help][markdown]" )
{
  HelpRegistry registry;
  HelpDescriptor d;
  d.id = QStringLiteral( "command.layer.saveEdits" );
  d.kind = HelpKind::Command;
  d.title = QStringLiteral( "保存编辑" );
  d.summary = QStringLiteral( "保存矢量编辑。" );
  CommandHelp c;
  c.purpose = QStringLiteral( "持久化修改" );
  d.command = c;
  REQUIRE( registry.registerDescriptor( d ) );

  const QString markdown = HelpMarkdownWriter::commandReference( registry );
  CHECK( markdown.contains( QStringLiteral( "## 保存编辑（command.layer.saveEdits）" ) ) );
  CHECK( markdown.contains( QStringLiteral( "持久化修改" ) ) );
  CHECK( markdown.contains( QStringLiteral( "自动生成" ) ) );

  // deterministic
  CHECK( markdown == HelpMarkdownWriter::commandReference( registry ) );
}

TEST_CASE( "terminology provider assigns distinct unique ids to duplicate en/slug terms without dropping entries", "[help][terms]" )
{
    QVector<TermEntry> terms;
    TermEntry t1;
    t1.en = QStringLiteral( "resolution" );
    t1.zh = QStringLiteral( "分辨率 (光谱)" );
    t1.definitionZh = QStringLiteral( "光谱采样间隔。" );
    t1.category = QStringLiteral( "spectral" );

    TermEntry t2;
    t2.en = QStringLiteral( "resolution" );
    t2.zh = QStringLiteral( "分辨率 (空间)" );
    t2.definitionZh = QStringLiteral( "地面采样距离。" );
    t2.category = QStringLiteral( "spatial" );

    TermEntry t3;
    t3.en = QStringLiteral( "pixel size" );
    t3.zh = QStringLiteral( "像元大小" );
    t3.definitionZh = QStringLiteral( "单个栅格单元尺寸。" );
    t3.category = QStringLiteral( "spatial" );
    t3.related = { QStringLiteral( "resolution" ) };

    terms << t1 << t2 << t3;

    HelpRegistry registry;
    QStringList errors;
    TerminologyProvider::appendDescriptors( terms, registry, &errors );

    INFO( "append errors: " << errors.join( QStringLiteral( "; " ) ).toStdString() );
    CHECK( errors.isEmpty() );
    // All 3 terms must be distinct registered descriptors; none dropped or overwritten!
    REQUIRE( registry.count() == 3 );
    CHECK( registry.find( QStringLiteral( "concept.term.resolution" ) ) != nullptr );
    CHECK( registry.find( QStringLiteral( "concept.term.resolution_2" ) ) != nullptr );
    CHECK( registry.find( QStringLiteral( "concept.term.pixel_size" ) ) != nullptr );

    // And related lookup must resolve without dangling references
    CHECK( registry.validateReferences().isEmpty() );
}

TEST_CASE( "summary truncation never splits a UTF-16 surrogate pair",
           "[help][descriptor][r17]" )
{
    // #1522: QString::left() counts CODE UNITS, so a cut landing between a
    // high and a low surrogate leaves a lone surrogate that renders as
    // U+FFFD. Both summary producers (DiagnosticCatalog::fallback() and the
    // curated mirror in HelpContentStore) must use truncateSummary() so the
    // boundary rule cannot drift apart again.
    using sicnu::help::truncateSummary;

    // 119 units of 'a', then one astral character (surrogate pair) occupying
    // units 119 and 120 — i.e. exactly straddling the 120-unit bound.
    QString text( 119, QLatin1Char( 'a' ) );
    text.append( QChar( 0xD83D ) ); // high surrogate
    text.append( QChar( 0xDE00 ) ); // low surrogate
    REQUIRE( text.size() == 121 );

    // The pre-fix behaviour, pinned so the regression cannot come back
    // silently: left(120) really does orphan the high surrogate.
    const QString naive = text.left( 120 );
    REQUIRE( naive.size() == 120 );
    REQUIRE( naive.at( 119 ).isHighSurrogate() );

    const QString truncated = truncateSummary( text );
    CHECK( truncated.size() == 119 );
    REQUIRE_FALSE( truncated.isEmpty() );
    // No lone surrogate at either end of the result.
    CHECK_FALSE( truncated.at( truncated.size() - 1 ).isHighSurrogate() );
    CHECK_FALSE( truncated.at( truncated.size() - 1 ).isLowSurrogate() );
    CHECK( truncated == QString( 119, QLatin1Char( 'a' ) ) );

    // A cut that does NOT straddle a pair is left at the full bound.
    const QString plain = truncateSummary( QString( 500, QLatin1Char( 'b' ) ) );
    CHECK( plain.size() == sicnu::help::kHelpSummaryMaxChars );

    // Shorter than the bound is returned untouched.
    const QString shortText = QStringLiteral( "short" );
    CHECK( truncateSummary( shortText ) == shortText );
}

TEST_CASE( "terminology descriptors respect the shared summary bound and surrogate rule",
           "[help][terms][r19d]" )
{
    // #1639 item 2: HelpDescriptor::summary documents a ≤kHelpSummaryMaxChars
    // bound shared by EVERY summary producer; terminology_provider used to
    // assign definitionZh verbatim, the one producer outside the contract.
    using sicnu::help::kHelpSummaryMaxChars;

    TermEntry t;
    t.en = QStringLiteral( "oversized definition term" );
    t.zh = QStringLiteral( "超长定义术语" );
    // 119 CJK chars (code units 0..118) + an astral char whose surrogate
    // pair occupies code units 119-120: size 121 exceeds the 120 bound AND
    // the naive left(120) cut lands exactly BETWEEN the pair's halves,
    // stranding the high surrogate. truncateSummary must back off to 119.
    t.definitionZh = QString( 119, QChar( 0x5B9A ) );
    t.definitionZh += QString( QChar( 0xD842 ) ) + QChar( 0xDFB7 );
    t.category = QStringLiteral( "spectral" );

    QVector<TermEntry> terms;
    terms << t;

    HelpRegistry registry;
    QStringList errors;
    TerminologyProvider::appendDescriptors( terms, registry, &errors );
    INFO( "append errors: " << errors.join( QStringLiteral( "; " ) ).toStdString() );
    CHECK( errors.isEmpty() );

    const HelpDescriptor *d =
      registry.find( termDescriptorId( t.en ) );
    REQUIRE( d != nullptr );

    CHECK( d->summary.size() <= kHelpSummaryMaxChars );
    // The boundary rule through appendDescriptors: the cut backs off the
    // straddling pair, so the summary is exactly the 119 CJK prefix and
    // contains no surrogate halves at all.
    CHECK( d->summary == t.definitionZh.left( 119 ) );
    // Belt-and-braces: no orphaned surrogate half anywhere in the summary.
    // (A COMPLETE astral char inside the window is fine — only a high
    // surrogate without its low partner, or a stray low, is an orphan.)
    bool orphan = false;
    for ( int i = 0; i < d->summary.size(); ++i )
    {
        const QChar c = d->summary.at( i );
        if ( c.isHighSurrogate() )
        {
            orphan = orphan || !( i + 1 < d->summary.size() &&
                                  d->summary.at( i + 1 ).isLowSurrogate() );
            ++i; // consume the pair's low half
        }
        else if ( c.isLowSurrogate() )
        {
            orphan = true; // standalone low: the high half was cut away
        }
    }
    CHECK_FALSE( orphan );
    // Truncation is lossless for short definitions (the real glossary shape).
    TermEntry shortTerm;
    shortTerm.en = QStringLiteral( "short definition term" );
    shortTerm.zh = QStringLiteral( "短定义术语" );
    shortTerm.definitionZh = QStringLiteral( "地面采样距离。" );
    shortTerm.category = QStringLiteral( "spatial" );
    QVector<TermEntry> shorts;
    shorts << shortTerm;
    TerminologyProvider::appendDescriptors( shorts, registry, &errors );
    const HelpDescriptor *sd =
      registry.find( termDescriptorId( shortTerm.en ) );
    REQUIRE( sd != nullptr );
    CHECK( sd->summary == shortTerm.definitionZh );
}
