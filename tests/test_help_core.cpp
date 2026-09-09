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

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QElapsedTimer>
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
  explanation.facts << AvailabilityFact{ QStringLiteral( "已选中图层" ), true };
  explanation.facts << AvailabilityFact{ QStringLiteral( "已选中矢量图层" ), true };
  explanation.facts << AvailabilityFact{ QStringLiteral( "编辑会话未开启" ), false };
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
