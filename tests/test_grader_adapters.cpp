// test_grader_adapters.cpp — RS14-05 Slices B/C: the pure JSON→JSON document
// adapters (ADR 0174 §8: "the module ships pure JSON→JSON adapters for the
// document shapes only"). Light lane: Catch2 + sicnu_grader only.
//
// The adapters project RECORDED documents — workflow provenance graphs
// (d17_provenance/1.0), workflow checkpoints (WorkflowRun serialization),
// metric records (run_id/protocol/metrics/metrics_hash/metrics_schema_version)
// and EvidenceProjector summaries — into sicnu.grader.evidence/1 items. They
// never invent evidence: absent members stay absent, non-recorded numbers are
// never computed, and hostile/malformed documents are typed refusals.
//
// RED-first record: no adapter surface exists on master (the grader ships
// engine/types/error/json/sha256 only), so every case below starts RED at
// compile/link time and stays honest via the shape contracts.
#include "grader/grader_adapters.h"
#include "grader/grader_engine.h"
#include "grader/grader_error.h"
#include "grader/grader_json.h"
#include "grader/grader_types.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::grader;

namespace {

/// A faithful d17_provenance/1.0 document, matching ProvenanceGraph::toJson()
/// (sorted nodes/edges omitted — adapters must not rely on order).
Json::Value provenanceDoc()
{
    Json::Value doc{ Json::objectValue };
    doc["kind"] = "d17_provenance";
    doc["version"] = "1.0";

    Json::Value run{ Json::objectValue };
    run["id"] = "run:exp-1#1";
    run["kind"] = "run";
    run["attributes"] = Json::Value{ Json::objectValue };
    run["attributes"]["workflowId"] = "wf-classify";
    run["attributes"]["workflowName"] = "Supervised classification";
    run["attributes"]["schemaVersion"] = 3;
    run["attributes"]["planSignature"] = "sig-abc123";

    Json::Value exec{ Json::objectValue };
    exec["id"] = "node:train";
    exec["kind"] = "nodeExec";
    exec["attributes"] = Json::Value{ Json::objectValue };
    exec["attributes"]["nodeId"] = "train";
    exec["attributes"]["operatorId"] = "op.train_classifier";
    exec["attributes"]["state"] = "Completed";
    exec["attributes"]["lineageSignature"] = "lin-1";
    exec["attributes"]["elapsedMs"] = 1520.0;
    exec["attributes"]["isCacheHit"] = false;

    Json::Value artifact{ Json::objectValue };
    artifact["id"] = "artifact:/runs/exp-1/model.tif";
    artifact["kind"] = "artifact";
    artifact["attributes"] = Json::Value{ Json::objectValue };
    artifact["attributes"]["path"] = "/runs/exp-1/model.tif";
    artifact["attributes"]["fingerprint"] = "fp-99";
    artifact["attributes"]["sizeBytes"] = 2048.0;

    Json::Value edge{ Json::objectValue };
    edge["from"] = "node:train";
    edge["to"] = "artifact:/runs/exp-1/model.tif";
    edge["kind"] = "produced";

    doc["nodes"] = Json::Value{ Json::arrayValue };
    doc["nodes"].append( run );
    doc["nodes"].append( exec );
    doc["nodes"].append( artifact );
    doc["edges"] = Json::Value{ Json::arrayValue };
    doc["edges"].append( edge );
    return doc;
}

/// A faithful WorkflowRun checkpoint document (WorkflowRun::toJson shape).
Json::Value checkpointDoc()
{
    Json::Value doc{ Json::objectValue };
    doc["version"] = 2;
    doc["runId"] = "exp-1";
    doc["workflowId"] = "wf-classify";
    doc["attempt"] = 1;
    doc["state"] = "Completed";
    doc["errorMessage"] = "";
    doc["progress"] = 1.0;
    doc["createdAt"] = "2026-09-23T08:00:00Z";
    doc["updatedAt"] = "2026-09-23T09:00:00Z";
    doc["definition"] = Json::Value{ Json::objectValue };
    Json::Value step{ Json::objectValue };
    step["stepId"] = "train";
    step["operatorId"] = "op.train_classifier";
    step["status"] = "Completed";
    step["cacheHit"] = false;
    step["fingerprint"] = "fp-step-1";
    step["outputDigest"] = "digest-1";
    step["outputSizeBytes"] = Json::Int64( 2048 );
    step["errorMessage"] = "";
    doc["stepPlans"] = Json::Value{ Json::arrayValue };
    doc["stepPlans"].append( step );
    doc["artifacts"] = Json::Value{ Json::objectValue };
    return doc;
}

/// A faithful metric-record document (MetricRecord::toJson shape).
Json::Value metricRecordDoc()
{
    Json::Value doc{ Json::objectValue };
    doc["run_id"] = "exp-1";
    doc["protocol"] = Json::Value{ Json::objectValue };
    doc["protocol"]["kind"] = "classification";
    Json::Value confusion{ Json::objectValue };
    confusion["overall_accuracy"] = 0.87;
    confusion["kappa"] = 0.81;
    doc["metrics"] = Json::Value{ Json::objectValue };
    doc["metrics"]["confusion_matrix"] = confusion;
    doc["metrics_hash"] = "mh-77";
    doc["metrics_schema_version"] = 1;
    return doc;
}

/// A faithful EvidenceProjector summary (evidence.h v1 layout).
Json::Value projectorDoc()
{
    Json::Value doc{ Json::objectValue };
    doc["schema_version"] = 1;
    doc["run_id"] = "exp-1";
    doc["experiment_id"] = "exp-1";
    doc["status"] = "Completed";
    Json::Value identity{ Json::objectValue };
    identity["model"] = "unet";
    identity["seed"] = 42;
    doc["identity"] = identity;
    Json::Value artifacts{ Json::objectValue };
    artifacts["path"] = "/runs/exp-1/model.tif";
    artifacts["role"] = "output";
    artifacts["digest"] = "fp-99";
    artifacts["size_bytes"] = 2048;
    doc["artifacts"] = Json::Value{ Json::arrayValue };
    doc["artifacts"].append( artifacts );
    Json::Value metrics{ Json::objectValue };
    metrics["hash"] = "mh-77";
    metrics["protocol"] = "classification";
    metrics["schema_version"] = 1;
    Json::Value document{ Json::objectValue };
    document["overall_accuracy"] = 0.87;
    metrics["document"] = document;
    doc["metrics"] = metrics;
    Json::Value steps{ Json::objectValue };
    steps["count"] = 2;
    steps["completed"] = 2;
    steps["failed"] = 0;
    doc["steps"] = steps;
    Json::Value completeness{ Json::objectValue };
    completeness["complete"] = true;
    doc["completeness"] = completeness;
    return doc;
}

} // namespace

TEST_CASE( "provenance adapter projects nodes/edges into grader evidence items",
           "[grader][adapters][provenance]" )
{
    GraderError err;
    const std::vector<GradeEvidenceItem> items = provenanceToEvidence( provenanceDoc(), err );
    REQUIRE( err.ok() );
    REQUIRE( items.size() == 3 );

    // Deterministic ids derived from the recorded run node + node ids
    // (namespaced so two runs of the same workflow merge without collisions).
    std::map<std::string, const GradeEvidenceItem *> byId;
    for ( const auto &item : items )
        byId[item.evidenceId] = &item;
    REQUIRE( byId.size() == items.size() ); // no duplicate ids
    REQUIRE( byId.count( "prov:run:exp-1#1:node:train" ) == 1 );
    const GradeEvidenceItem &exec = *byId.at( "prov:run:exp-1#1:node:train" );
    CHECK( exec.kind == EvidenceKind::Stage );
    CHECK( exec.key == "train" ); // matcher key = the nodeExec's nodeId
    CHECK( exec.state == "Completed" );
    CHECK( exec.facts["operatorId"].asString() == "op.train_classifier" );
    CHECK( exec.facts["isCacheHit"].asBool() == false );

    REQUIRE( byId.count( "prov:run:exp-1#1:artifact:/runs/exp-1/model.tif" ) == 1 );
    const GradeEvidenceItem &artifact = *byId.at( "prov:run:exp-1#1:artifact:/runs/exp-1/model.tif" );
    CHECK( artifact.kind == EvidenceKind::ArtifactState );
    CHECK( artifact.key == "/runs/exp-1/model.tif" );
    CHECK( artifact.facts["fingerprint"].asString() == "fp-99" );
    CHECK( artifact.facts["producedBy"][0].asString() == "node:train" );

    REQUIRE( byId.count( "prov:run:exp-1#1" ) == 1 );
    const GradeEvidenceItem &run = *byId.at( "prov:run:exp-1#1" );
    CHECK( run.kind == EvidenceKind::Custom );
    CHECK( run.facts["planSignature"].asString() == "sig-abc123" );

    // The projected bundle validates and grades end-to-end against a stage
    // rubric keyed on the nodeExec nodeId — recorded truth drives the verdict.
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    evidence.items = items;
    REQUIRE( evidence.validate( err ) );
    GradingRubric rubric;
    rubric.rubricId = "prov-lab";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "proc-train";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Stage;
    c.evidenceKey = "train";
    c.stage.expectedState = "Completed";
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    const GradeOutcome outcome = grade( rubric, evidence );
    CAPTURE( outcome.error.message );
    REQUIRE( outcome.ok );
    CHECK( outcome.report.score == 100.0 );
}

TEST_CASE( "provenance adapter refuses foreign envelopes and malformed documents",
           "[grader][adapters][provenance][hostile]" )
{
    GraderError err;

    SECTION( "foreign kind" )
    {
        Json::Value doc = provenanceDoc();
        doc["kind"] = "something_else";
        const auto items = provenanceToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( toCodeString( err.code ) == "grader:e-schema-shape" );
    }

    SECTION( "foreign version" )
    {
        Json::Value doc = provenanceDoc();
        doc["version"] = "9.9";
        const auto items = provenanceToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "non-object document" )
    {
        Json::Value doc{ Json::arrayValue };
        const auto items = provenanceToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "node without id" )
    {
        Json::Value doc = provenanceDoc();
        doc["nodes"][1].removeMember( "id" );
        const auto items = provenanceToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( err.path.find( "nodes" ) != std::string::npos );
    }

    SECTION( "unknown node kind" )
    {
        Json::Value doc = provenanceDoc();
        doc["nodes"][1]["kind"] = "button";
        const auto items = provenanceToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "dangling edge is projected as fact-free membership, not a crash" )
    {
        // The adapter projects the recorded graph; unlike the Qt-side strict
        // reader it does not need edge resolution to hold — but a dangling
        // edge must not fabricate producer facts.
        Json::Value doc = provenanceDoc();
        doc["edges"][0]["from"] = "node:ghost";
        const auto items = provenanceToEvidence( doc, err );
        REQUIRE( err.ok() );
        bool sawArtifact = false;
        for ( const auto &item : items ) {
            if ( item.kind == EvidenceKind::ArtifactState ) {
                sawArtifact = true;
                CHECK( item.facts["producedBy"].isNull() );
            }
        }
        CHECK( sawArtifact );
    }
}

TEST_CASE( "checkpoint adapter projects run state and step plans",
           "[grader][adapters][checkpoint]" )
{
    GraderError err;
    const std::vector<GradeEvidenceItem> items = checkpointToEvidence( checkpointDoc(), err );
    REQUIRE( err.ok() );
    REQUIRE( items.size() == 2 );

    std::map<std::string, const GradeEvidenceItem *> byId;
    for ( const auto &item : items )
        byId[item.evidenceId] = &item;

    // Run-level item: state from the recorded envelope.
    REQUIRE( byId.count( "ckpt:exp-1:run" ) == 1 );
    const GradeEvidenceItem &run = *byId.at( "ckpt:exp-1:run" );
    CHECK( run.kind == EvidenceKind::Custom );
    CHECK( run.key == "exp-1" );
    CHECK( run.state == "Completed" );
    CHECK( run.facts["workflowId"].asString() == "wf-classify" );

    // Step-plan item: stage evidence keyed by stepId.
    REQUIRE( byId.count( "ckpt:exp-1:step:train" ) == 1 );
    const GradeEvidenceItem &step = *byId.at( "ckpt:exp-1:step:train" );
    CHECK( step.kind == EvidenceKind::Stage );
    CHECK( step.key == "train" );
    CHECK( step.state == "Completed" );
    CHECK( step.facts["operatorId"].asString() == "op.train_classifier" );
    CHECK( step.facts["outputDigest"].asString() == "digest-1" );

    // End-to-end: adapter output grades a stage rubric to a pass.
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    evidence.items = items;
    REQUIRE( evidence.validate( err ) );
    GradingRubric rubric;
    rubric.rubricId = "ckpt-lab";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "proc-train";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Stage;
    c.evidenceKey = "train";
    c.stage.expectedState = "Completed";
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    const GradeOutcome outcome = grade( rubric, evidence );
    REQUIRE( outcome.ok );
    CHECK( outcome.report.score == 100.0 );
}

TEST_CASE( "checkpoint adapter refuses structurally broken documents",
           "[grader][adapters][checkpoint][hostile]" )
{
    GraderError err;

    SECTION( "missing runId" )
    {
        Json::Value doc = checkpointDoc();
        doc.removeMember( "runId" );
        const auto items = checkpointToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "non-string runId" )
    {
        Json::Value doc = checkpointDoc();
        doc["runId"] = 42;
        const auto items = checkpointToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "missing serialization version" )
    {
        Json::Value doc = checkpointDoc();
        doc.removeMember( "version" );
        const auto items = checkpointToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "unsupported serialization version" )
    {
        Json::Value doc = checkpointDoc();
        doc["version"] = 99;
        const auto items = checkpointToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "stepPlans of non-array type" )
    {
        Json::Value doc = checkpointDoc();
        doc["stepPlans"] = "all-of-them";
        const auto items = checkpointToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "step without stepId" )
    {
        Json::Value doc = checkpointDoc();
        doc["stepPlans"][0].removeMember( "stepId" );
        const auto items = checkpointToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }
}

TEST_CASE( "metric-record adapter projects numeric metric observations",
           "[grader][adapters][metrics]" )
{
    GraderError err;
    const std::vector<GradeEvidenceItem> items = metricRecordToEvidence( metricRecordDoc(), err );
    REQUIRE( err.ok() );
    REQUIRE( items.size() == 2 ); // confusion_matrix.overall_accuracy + confusion_matrix.kappa

    std::map<std::string, double> values;
    for ( const auto &item : items ) {
        CHECK( item.kind == EvidenceKind::Metric );
        CHECK( item.hasValue );
        CHECK( item.source == "metric_record:exp-1:mh-77" );
        values[item.key] = item.value;
    }
    REQUIRE( values.count( "confusion_matrix.overall_accuracy" ) == 1 );
    CHECK( values.at( "confusion_matrix.overall_accuracy" ) == 0.87 );
    REQUIRE( values.count( "confusion_matrix.kappa" ) == 1 );
    CHECK( values.at( "confusion_matrix.kappa" ) == 0.81 );

    // End-to-end: a metric criterion grades the projected observation.
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    evidence.items = items;
    REQUIRE( evidence.validate( err ) );
    GradingRubric rubric;
    rubric.rubricId = "metric-lab";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "res-acc";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Metric;
    c.evidenceKey = "confusion_matrix.overall_accuracy";
    c.metric.mode = MetricExpectation::Mode::AtLeast;
    c.metric.value = 0.85;
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    const GradeOutcome outcome = grade( rubric, evidence );
    REQUIRE( outcome.ok );
    CHECK( outcome.report.score == 100.0 );
}

TEST_CASE( "metric-record adapter honors the metrics schema version and refuses garbage",
           "[grader][adapters][metrics][hostile]" )
{
    GraderError err;

    SECTION( "foreign metrics schema version refused" )
    {
        Json::Value doc = metricRecordDoc();
        doc["metrics_schema_version"] = 2;
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( toCodeString( err.code ) == "grader:e-schema-version" );
    }

    SECTION( "missing metrics_hash refused (the binding is recorded truth)" )
    {
        Json::Value doc = metricRecordDoc();
        doc.removeMember( "metrics_hash" );
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "metrics of non-object type refused" )
    {
        Json::Value doc = metricRecordDoc();
        doc["metrics"] = "great";
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "empty metric name refused" )
    {
        Json::Value doc = metricRecordDoc();
        doc["metrics"][""] = 0.5;
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "numeric scalar metric document projects directly" )
    {
        Json::Value doc = metricRecordDoc();
        doc["metrics"] = Json::Value{ Json::objectValue };
        doc["metrics"]["overall_accuracy"] = 0.9;
        const auto items = metricRecordToEvidence( doc, err );
        REQUIRE( err.ok() );
        REQUIRE( items.size() == 1 );
        CHECK( items[0].key == "overall_accuracy" );
        CHECK( items[0].value == 0.9 );
    }

    SECTION( "deeply nested numeric leaves project with dotted keys, capped depth" )
    {
        Json::Value doc = metricRecordDoc();
        Json::Value deep{ Json::objectValue };
        Json::Value *cursor = &deep;
        for ( int i = 0; i < 20; ++i ) {
            ( *cursor )["n"] = Json::Value{ Json::objectValue };
            cursor = &( ( *cursor )["n"] );
        }
        ( *cursor )["leaf"] = 0.42;
        doc["metrics"] = Json::Value{ Json::objectValue };
        doc["metrics"]["deep_metric"] = deep;
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() ); // past the depth cap → typed refusal, no silent truncation
    }

    SECTION( "distinct recorded members collapsing onto one dotted key are a typed refusal" )
    {
        Json::Value doc = metricRecordDoc();
        doc["metrics"] = Json::Value{ Json::objectValue };
        doc["metrics"]["a"] = Json::Value{ Json::objectValue };
        doc["metrics"]["a"]["b"] = 1.0;
        doc["metrics"]["a.b"] = 2.0;
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( err.message.find( "duplicate key" ) != std::string::npos );
    }

    SECTION( "two runs sharing one metrics hash merge without id collisions" )
    {
        // metrics_hash is a content hash — a deterministic workflow produces
        // the same hash on every run; identity comes from run_id.
        Json::Value docB = metricRecordDoc();
        docB["run_id"] = "exp-2";
        const auto itemsA = metricRecordToEvidence( metricRecordDoc(), err );
        REQUIRE( err.ok() );
        const auto itemsB = metricRecordToEvidence( docB, err );
        REQUIRE( err.ok() );

        GradeEvidence merged;
        merged.subject.experimentId = "exp-1";
        merged.items = itemsA;
        merged.items.insert( merged.items.end(), itemsB.begin(), itemsB.end() );
        REQUIRE( merged.validate( err ) );

        // A metric criterion sees two agreeing observations on each key and
        // grades deterministically (stable tie-break, both cited).
        GradingRubric rubric;
        rubric.rubricId = "merge-metric";
        rubric.revision = 1;
        rubric.totalPoints = 100.0;
        rubric.passingScore = 60.0;
        Dimension dim;
        dim.dimensionId = "d";
        dim.weight = 100.0;
        Criterion c;
        c.criterionId = "res-acc";
        c.maxPoints = 100.0;
        c.kind = CriterionKind::Metric;
        c.evidenceKey = "confusion_matrix.overall_accuracy";
        c.metric.mode = MetricExpectation::Mode::AtLeast;
        c.metric.value = 0.85;
        dim.criteria.push_back( c );
        rubric.dimensions.push_back( dim );
        const GradeOutcome outcome = grade( rubric, merged );
        CAPTURE( outcome.error.message );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].evidenceIds.size() == 2 );
        CHECK( outcome.report.score == 100.0 );
    }

    SECTION( "metric records without run_id are typed refusals" )
    {
        Json::Value doc = metricRecordDoc();
        doc.removeMember( "run_id" );
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( err.path == "run_id" );
    }

    SECTION( "non-finite numbers inside the record are typed refusals" )
    {
        Json::Value doc = metricRecordDoc();
        doc["metrics"]["confusion_matrix"]["kappa"] = Json::Value( std::numeric_limits<double>::infinity() );
        const auto items = metricRecordToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }
}

TEST_CASE( "EvidenceProjector summary adapter projects recorded dimensions",
           "[grader][adapters][projector]" )
{
    GraderError err;
    const std::vector<GradeEvidenceItem> items = projectorSummaryToEvidence( projectorDoc(), err );
    REQUIRE( err.ok() );

    std::map<std::string, const GradeEvidenceItem *> byId;
    for ( const auto &item : items )
        byId[item.evidenceId] = &item;

    // Run status item.
    REQUIRE( byId.count( "proj:exp-1:status" ) == 1 );
    CHECK( byId.at( "proj:exp-1:status" )->state == "Completed" );

    // Identity dimension.
    REQUIRE( byId.count( "proj:exp-1:identity" ) == 1 );
    CHECK( byId.at( "proj:exp-1:identity" )->facts["model"].asString() == "unet" );

    // Artifact projection with role + digest.
    REQUIRE( byId.count( "proj:exp-1:artifact:/runs/exp-1/model.tif" ) == 1 );
    const GradeEvidenceItem &artifact = *byId.at( "proj:exp-1:artifact:/runs/exp-1/model.tif" );
    CHECK( artifact.kind == EvidenceKind::ArtifactState );
    CHECK( artifact.facts["digest"].asString() == "fp-99" );

    // Metric document projected as numeric observations.
    bool sawMetric = false;
    for ( const auto &item : items ) {
        if ( item.kind == EvidenceKind::Metric && item.key == "overall_accuracy" ) {
            sawMetric = true;
            CHECK( item.value == 0.87 );
        }
    }
    CHECK( sawMetric );

    // Steps dimension.
    REQUIRE( byId.count( "proj:exp-1:steps" ) == 1 );
    CHECK( byId.at( "proj:exp-1:steps" )->facts["completed"].asInt() == 2 );

    // End-to-end grade: artifact digest fact criterion over projected items.
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    evidence.items = items;
    REQUIRE( evidence.validate( err ) );
    GradingRubric rubric;
    rubric.rubricId = "proj-lab";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "fact-model";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Fact;
    c.evidenceKey = "identity";
    c.fact.expectedState = "";
    c.fact.requiredFacts["model"] = "\"unet\"";
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    const GradeOutcome outcome = grade( rubric, evidence );
    CAPTURE( outcome.error.message );
    REQUIRE( outcome.ok );
    CHECK( outcome.report.score == 100.0 );
}

TEST_CASE( "EvidenceProjector adapter refuses foreign schema versions and hostile shapes",
           "[grader][adapters][projector][hostile]" )
{
    GraderError err;

    SECTION( "foreign schema_version" )
    {
        Json::Value doc = projectorDoc();
        doc["schema_version"] = 2;
        const auto items = projectorSummaryToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( toCodeString( err.code ) == "grader:e-schema-version" );
    }

    SECTION( "missing run_id" )
    {
        Json::Value doc = projectorDoc();
        doc.removeMember( "run_id" );
        const auto items = projectorSummaryToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "artifacts of non-array type" )
    {
        Json::Value doc = projectorDoc();
        doc["artifacts"] = "many";
        const auto items = projectorSummaryToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "artifact without path" )
    {
        Json::Value doc = projectorDoc();
        doc["artifacts"][0].removeMember( "path" );
        const auto items = projectorSummaryToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }

    SECTION( "two artifacts sharing one path are a typed refusal" )
    {
        Json::Value doc = projectorDoc();
        doc["artifacts"].append( doc["artifacts"][0] );
        const auto items = projectorSummaryToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
        CHECK( err.message.find( "duplicate artifact path" ) != std::string::npos );
    }

    SECTION( "a missing metrics dimension (empty object, the projector's absence spelling) projects NO fabricated item" )
    {
        Json::Value doc = projectorDoc();
        doc["metrics"] = Json::Value{ Json::objectValue }; // real absence spelling
        const auto items = projectorSummaryToEvidence( doc, err );
        REQUIRE( err.ok() );
        for ( const auto &item : items )
            CHECK( item.kind != EvidenceKind::Metric );
    }

    SECTION( "metrics of non-object type is a hostile shape, not an absence" )
    {
        Json::Value doc = projectorDoc();
        doc["metrics"] = "missing";
        const auto items = projectorSummaryToEvidence( doc, err );
        CHECK_FALSE( err.ok() );
    }
}

TEST_CASE( "merged adapter output stays within evidence budgets and dedupes",
           "[grader][adapters][composition]" )
{
    // Two runs of the same workflow shape with distinct run ids merge into
    // one bundle WITHOUT evidenceId collisions (run-namespaced ids).
    GraderError err;
    Json::Value docA = provenanceDoc();
    Json::Value docB = provenanceDoc();
    docB["nodes"][0]["id"] = "run:exp-2#1";

    std::vector<GradeEvidenceItem> items = provenanceToEvidence( docA, err );
    REQUIRE( err.ok() );
    const auto itemsB = provenanceToEvidence( docB, err );
    REQUIRE( err.ok() );
    items.insert( items.end(), itemsB.begin(), itemsB.end() );

    std::set<std::string> ids;
    for ( const auto &item : items )
        ids.insert( item.evidenceId );
    CHECK( ids.size() == items.size() ); // no collisions across the merged runs

    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    evidence.items = items;
    REQUIRE( evidence.validate( err ) );

    // Both runs' train stages are present as distinct records; a rubric
    // criterion on key "train" sees two agreeing stage observations and
    // still earns deterministically.
    GradingRubric rubric;
    rubric.rubricId = "merge-lab";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "proc-train";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Stage;
    c.evidenceKey = "train";
    c.stage.expectedState = "Completed";
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    const GradeOutcome outcome = grade( rubric, evidence );
    REQUIRE( outcome.ok );
    CHECK( outcome.report.dimensions[0].criteria[0].evidenceIds.size() == 2 );
    CHECK( outcome.report.score == 100.0 );
}

// ---------------------------------------------------------------------------
// E12 teaching behavior benchmark (WP E12, D17)
//
// The grader-side half of the direction's behavior benchmark: one NDVI
// teaching scenario (NoData handling, cloud mask, confusion-matrix accuracy,
// spatial-leakage declaration, CRS match, sidecar digest) hand-built from
// the adapter documents above plus the recorded declarations the d17
// projection does not carry. Clean evidence must earn every criterion with
// an empty reason chain; each single-fault variant must flip EXACTLY its own
// criterion with the precise reason slug(s) — never just any failure; and
// repeated grading must reproduce the report digest byte-for-byte.
// ---------------------------------------------------------------------------

namespace {

/// d17_provenance/1.0 for the NDVI accuracy lab: cloud mask → NDVI →
/// accuracy evaluation, plus the cloud-mask and inference-sidecar artifacts.
Json::Value ndviProvenanceDoc( bool includeCloudMask = true )
{
    Json::Value doc{ Json::objectValue };
    doc["kind"] = "d17_provenance";
    doc["version"] = "1.0";

    Json::Value run{ Json::objectValue };
    run["id"] = "run:exp-ndvi-1#1";
    run["kind"] = "run";
    run["attributes"] = Json::Value{ Json::objectValue };
    run["attributes"]["workflowId"] = "wf-ndvi-accuracy";
    run["attributes"]["workflowName"] = "NDVI accuracy lab";
    run["attributes"]["schemaVersion"] = 3;
    run["attributes"]["planSignature"] = "sig-ndvi-1";
    doc["nodes"] = Json::Value{ Json::arrayValue };
    doc["nodes"].append( run );

    auto execNode = []( const char *id, const char *nodeId, const char *operatorId ) {
        Json::Value exec{ Json::objectValue };
        exec["id"] = id;
        exec["kind"] = "nodeExec";
        exec["attributes"] = Json::Value{ Json::objectValue };
        exec["attributes"]["nodeId"] = nodeId;
        exec["attributes"]["operatorId"] = operatorId;
        exec["attributes"]["state"] = "Completed";
        exec["attributes"]["isCacheHit"] = false;
        return exec;
    };
    if ( includeCloudMask )
        doc["nodes"].append( execNode( "node:cloud_mask", "cloud_mask", "op.cloud_mask" ) );
    doc["nodes"].append( execNode( "node:ndvi", "ndvi", "op.ndvi" ) );
    doc["nodes"].append( execNode( "node:accuracy_eval", "accuracy_eval", "op.accuracy_eval" ) );

    auto artifactNode = []( const char *path, const char *fingerprint ) {
        Json::Value artifact{ Json::objectValue };
        artifact["id"] = std::string( "artifact:" ) + path;
        artifact["kind"] = "artifact";
        artifact["attributes"] = Json::Value{ Json::objectValue };
        artifact["attributes"]["path"] = path;
        artifact["attributes"]["fingerprint"] = fingerprint;
        return artifact;
    };
    if ( includeCloudMask )
        doc["nodes"].append( artifactNode( "/runs/exp-ndvi-1/cloud_mask.tif", "fp-mask-1" ) );
    doc["nodes"].append( artifactNode( "/runs/exp-ndvi-1/inference_sidecar.json",
                                       "sha256:sidecar-aa11" ) );

    doc["edges"] = Json::Value{ Json::arrayValue };
    auto producedEdge = []( const char *from, const char *to ) {
        Json::Value edge{ Json::objectValue };
        edge["from"] = from;
        edge["to"] = to;
        edge["kind"] = "produced";
        return edge;
    };
    if ( includeCloudMask )
        doc["edges"].append( producedEdge( "node:cloud_mask",
                                           "artifact:/runs/exp-ndvi-1/cloud_mask.tif" ) );
    doc["edges"].append( producedEdge( "node:accuracy_eval",
                                       "artifact:/runs/exp-ndvi-1/inference_sidecar.json" ) );
    return doc;
}

/// WorkflowRun checkpoint (v2) for the same lab, mirroring the stage keys.
Json::Value ndviCheckpointDoc( bool includeCloudMask = true )
{
    Json::Value doc{ Json::objectValue };
    doc["version"] = 2;
    doc["runId"] = "exp-ndvi-1";
    doc["workflowId"] = "wf-ndvi-accuracy";
    doc["attempt"] = 1;
    doc["state"] = "Completed";
    doc["progress"] = 1.0;
    doc["createdAt"] = "2026-10-01T08:00:00Z";
    doc["updatedAt"] = "2026-10-01T09:00:00Z";
    doc["definition"] = Json::Value{ Json::objectValue };

    auto step = []( const char *stepId, const char *operatorId ) {
        Json::Value plan{ Json::objectValue };
        plan["stepId"] = stepId;
        plan["operatorId"] = operatorId;
        plan["status"] = "Completed";
        plan["cacheHit"] = false;
        plan["fingerprint"] = std::string( "fp-" ) + stepId;
        plan["outputDigest"] = std::string( "digest-" ) + stepId;
        plan["outputSizeBytes"] = Json::Int64( 1024 );
        plan["errorMessage"] = "";
        return plan;
    };
    doc["stepPlans"] = Json::Value{ Json::arrayValue };
    if ( includeCloudMask )
        doc["stepPlans"].append( step( "cloud_mask", "op.cloud_mask" ) );
    doc["stepPlans"].append( step( "ndvi", "op.ndvi" ) );
    doc["stepPlans"].append( step( "accuracy_eval", "op.accuracy_eval" ) );
    doc["artifacts"] = Json::Value{ Json::objectValue };
    return doc;
}

/// Metric record for the lab. `ignoreNoData` models the classic fault: the
/// accuracy was computed on raw pixels with the NoData sentinel left in.
Json::Value ndviMetricDoc( bool ignoreNoData = false )
{
    Json::Value doc{ Json::objectValue };
    doc["run_id"] = "exp-ndvi-1";
    doc["protocol"] = Json::Value{ Json::objectValue };
    doc["protocol"]["kind"] = "classification";
    Json::Value confusion{ Json::objectValue };
    confusion["overall_accuracy"] = 0.91;
    confusion["kappa"] = 0.86;
    Json::Value ndvi{ Json::objectValue };
    ndvi["nodata_aware_accuracy"] = ignoreNoData ? 0.71 : 0.91;
    Json::Value sidecar{ Json::objectValue };
    sidecar["digest_match"] = 1.0;
    doc["metrics"] = Json::Value{ Json::objectValue };
    doc["metrics"]["confusion_matrix"] = confusion;
    doc["metrics"]["ndvi"] = ndvi;
    doc["metrics"]["sidecar"] = sidecar;
    doc["metrics_hash"] = ignoreNoData ? "mh-ndvi-1b" : "mh-ndvi-1";
    doc["metrics_schema_version"] = 1;
    return doc;
}

/// Recorded spatial-split declaration (the d17 artifact projection carries
/// path/fingerprint/size only, so the declaration arrives as its own item,
/// mirroring the recorded spatial_split.json the lab writes).
GradeEvidenceItem spatialSplitItem( const char *leakage )
{
    GradeEvidenceItem item;
    item.evidenceId = "prov:run:exp-ndvi-1#1:artifact:/runs/exp-ndvi-1/spatial_split.json";
    item.kind = EvidenceKind::ArtifactState;
    item.key = "spatial_split";
    item.state = "present";
    item.facts["path"] = "/runs/exp-ndvi-1/spatial_split.json";
    item.facts["leakage"] = leakage;
    item.source = "d17_provenance";
    return item;
}

/// Recorded CRS check against the scene's reference CRS.
GradeEvidenceItem crsCheckItem( const char *state )
{
    GradeEvidenceItem item;
    item.evidenceId = "prov:run:exp-ndvi-1#1:check:crs";
    item.kind = EvidenceKind::Provenance;
    item.key = "crs_check";
    item.state = state; // "matched" | "mismatch"
    item.facts["crs"] = "EPSG:32648";
    item.facts["expected"] = "EPSG:32648";
    item.source = "d17_provenance";
    return item;
}

/// The full teaching bundle: all three adapter projections plus the two
/// recorded declarations. Every item is rebuilt from documents on each call
/// so mutations can never leak between benchmark runs.
std::vector<GradeEvidenceItem> ndviEvidenceItems( const Json::Value &provenanceDoc,
                                                  const Json::Value &checkpointDoc,
                                                  const Json::Value &metricDoc,
                                                  const char *leakage, const char *crsState )
{
    GraderError err;
    std::vector<GradeEvidenceItem> items = provenanceToEvidence( provenanceDoc, err );
    REQUIRE( err.ok() );
    const auto checkpoint = checkpointToEvidence( checkpointDoc, err );
    REQUIRE( err.ok() );
    items.insert( items.end(), checkpoint.begin(), checkpoint.end() );
    const auto metrics = metricRecordToEvidence( metricDoc, err );
    REQUIRE( err.ok() );
    items.insert( items.end(), metrics.begin(), metrics.end() );
    items.push_back( spatialSplitItem( leakage ) );
    items.push_back( crsCheckItem( crsState ) );
    return items;
}

std::vector<GradeEvidenceItem> ndviEvidenceItems()
{
    return ndviEvidenceItems( ndviProvenanceDoc(), ndviCheckpointDoc(), ndviMetricDoc(), "none",
                              "matched" );
}

/// Six criteria, 100 points: process (cloud mask, leakage, CRS), results
/// (raw + NoData-aware accuracy), artifact integrity (sidecar digest).
GradingRubric ndviRubric()
{
    GradingRubric rubric;
    rubric.rubricId = "ndvi-lab-e12";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;

    Criterion cloudMask;
    cloudMask.criterionId = "proc-cloud-mask";
    cloudMask.maxPoints = 15.0;
    cloudMask.kind = CriterionKind::Stage;
    cloudMask.evidenceKey = "cloud_mask";
    cloudMask.stage.expectedState = "Completed";
    dim.criteria.push_back( cloudMask );

    Criterion accuracy;
    accuracy.criterionId = "res-accuracy";
    accuracy.maxPoints = 25.0;
    accuracy.kind = CriterionKind::Metric;
    accuracy.evidenceKey = "confusion_matrix.overall_accuracy";
    accuracy.metric.mode = MetricExpectation::Mode::AtLeast;
    accuracy.metric.value = 0.85;
    dim.criteria.push_back( accuracy );

    Criterion nodataAccuracy;
    nodataAccuracy.criterionId = "res-nodata-accuracy";
    nodataAccuracy.maxPoints = 20.0;
    nodataAccuracy.kind = CriterionKind::Metric;
    nodataAccuracy.evidenceKey = "ndvi.nodata_aware_accuracy";
    nodataAccuracy.metric.mode = MetricExpectation::Mode::AtLeast;
    nodataAccuracy.metric.value = 0.85;
    dim.criteria.push_back( nodataAccuracy );

    Criterion leakage;
    leakage.criterionId = "proc-leakage";
    leakage.maxPoints = 15.0;
    leakage.kind = CriterionKind::Fact;
    leakage.evidenceKey = "spatial_split";
    leakage.requireEvidenceKind = EvidenceKind::ArtifactState;
    leakage.fact.expectedState = "present";
    leakage.fact.requiredFacts["leakage"] = "\"none\"";
    dim.criteria.push_back( leakage );

    Criterion crs;
    crs.criterionId = "proc-crs";
    crs.maxPoints = 10.0;
    crs.kind = CriterionKind::Fact;
    crs.evidenceKey = "crs_check";
    crs.requireEvidenceKind = EvidenceKind::Provenance;
    crs.fact.expectedState = "matched";
    crs.fact.requiredFacts["crs"] = "\"EPSG:32648\"";
    dim.criteria.push_back( crs );

    Criterion sidecar;
    sidecar.criterionId = "art-sidecar";
    sidecar.maxPoints = 15.0;
    sidecar.kind = CriterionKind::Metric;
    sidecar.evidenceKey = "sidecar.digest_match";
    sidecar.metric.mode = MetricExpectation::Mode::Equals;
    sidecar.metric.value = 1.0;
    dim.criteria.push_back( sidecar );

    rubric.dimensions.push_back( dim );
    return rubric;
}

const CriterionOutcome *findCriterion( const GradeReport &report, const std::string &criterionId )
{
    for ( const auto &dimension : report.dimensions )
        for ( const auto &criterion : dimension.criteria )
            if ( criterion.criterionId == criterionId )
                return &criterion;
    return nullptr;
}

/// Grades one evidence bundle against the NDVI rubric (the rubric is rebuilt
/// per call so a caller-side mutation can never leak into the next grade).
GradeOutcome gradeNdvi( const std::vector<GradeEvidenceItem> &items )
{
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-ndvi-1";
    evidence.items = items;
    GraderError err;
    REQUIRE( evidence.validate( err ) );
    CAPTURE( err.message );
    const GradeOutcome outcome = grade( ndviRubric(), evidence );
    CAPTURE( outcome.error.message );
    REQUIRE( outcome.ok );
    return outcome;
}

/// The six criterion ids, in rubric order.
const std::vector<std::string> kNdviCriteria = { "proc-cloud-mask", "res-accuracy",
                                                 "res-nodata-accuracy", "proc-leakage", "proc-crs",
                                                 "art-sidecar" };

/// Asserts the report flips exactly `flipped` — with the precise reason
/// slug(s) — while every other criterion stays earned with an empty reason
/// chain (the REASON CHAIN contract: full points carry no slugs).
void checkSingleFault( const GradeReport &report, const std::string &flipped,
                       const std::vector<std::string> &expectedSlugs, double expectedScore )
{
    for ( const std::string &criterionId : kNdviCriteria ) {
        const CriterionOutcome *outcome = findCriterion( report, criterionId );
        REQUIRE( outcome != nullptr );
        if ( criterionId == flipped ) {
            CHECK( outcome->status != OutcomeStatus::Earned );
            CHECK( outcome->reasonCodes == expectedSlugs );
            CHECK( outcome->rawEarned == 0.0 );
        }
        else {
            CHECK( outcome->status == OutcomeStatus::Earned );
            CHECK( outcome->reasonCodes.empty() );
            CHECK( outcome->rawEarned == outcome->maxPoints );
        }
    }
    CHECK( report.score == expectedScore );
}

} // namespace

TEST_CASE( "teaching benchmark: clean NDVI evidence earns every criterion with an empty reason chain",
           "[grader][adapters][teaching][benchmark]" )
{
    const GradeOutcome outcome = gradeNdvi( ndviEvidenceItems() );
    const GradeReport &report = outcome.report;

    CHECK( report.score == 100.0 );
    CHECK( report.verdict == ReportVerdict::Pass );
    for ( const std::string &criterionId : kNdviCriteria ) {
        const CriterionOutcome *criterion = findCriterion( report, criterionId );
        REQUIRE( criterion != nullptr );
        CHECK( criterion->status == OutcomeStatus::Earned );
        CHECK( criterion->rawEarned == criterion->maxPoints );
        // The expected reason-slug set for an earned criterion is EMPTY: the
        // REASON CHAIN contract pays slugs only for points not earned.
        CHECK( criterion->reasonCodes.empty() );
        // ...and every earned point is cited against recorded evidence.
        CHECK( !criterion->evidenceIds.empty() );
    }
}

TEST_CASE( "teaching benchmark: each single-fault variant flips exactly its criterion with the precise reason slug",
           "[grader][adapters][teaching][benchmark]" )
{
    SECTION( "NoData-ignored accuracy is below the declared band" )
    {
        // The same lab recomputed with the NoData sentinel left in the
        // pixels: the recorded NoData-aware accuracy collapses to 0.71.
        const GradeOutcome outcome = gradeNdvi( ndviEvidenceItems(
            ndviProvenanceDoc(), ndviCheckpointDoc(), ndviMetricDoc( /*ignoreNoData*/ true ), "none",
            "matched" ) );
        checkSingleFault( outcome.report, "res-nodata-accuracy", { "grader:metric-below-band" },
                          80.0 );
    }

    SECTION( "missing cloud mask leaves the stage unrecorded" )
    {
        const GradeOutcome outcome = gradeNdvi( ndviEvidenceItems(
            ndviProvenanceDoc( /*includeCloudMask*/ false ),
            ndviCheckpointDoc( /*includeCloudMask*/ false ), ndviMetricDoc(), "none", "matched" ) );
        checkSingleFault( outcome.report, "proc-cloud-mask", { "grader:stage-missing" }, 85.0 );
    }

    SECTION( "leakage-declared provenance contradicts the required declaration" )
    {
        const GradeOutcome outcome = gradeNdvi(
            ndviEvidenceItems( ndviProvenanceDoc(), ndviCheckpointDoc(), ndviMetricDoc(), "declared",
                               "matched" ) );
        checkSingleFault( outcome.report, "proc-leakage",
                          { "grader:fact-missing-required-fact" }, 85.0 );
    }

    SECTION( "CRS mismatch record never carries the expected state" )
    {
        const GradeOutcome outcome = gradeNdvi( ndviEvidenceItems(
            ndviProvenanceDoc(), ndviCheckpointDoc(), ndviMetricDoc(), "none", "mismatch" ) );
        checkSingleFault( outcome.report, "proc-crs", { "grader:fact-state-mismatch" }, 90.0 );
    }

    SECTION( "tampered sidecar digest makes the recorded observations conflict" )
    {
        // A re-issued metric record after the sidecar was swapped contradicts
        // the original observation: conflicting recorded truth is
        // indeterminate, never silently resolved to one side.
        std::vector<GradeEvidenceItem> items = ndviEvidenceItems();
        GradeEvidenceItem tampered;
        tampered.evidenceId = "metric_record:exp-ndvi-1:mh-ndvi-1-tampered:sidecar.digest_match";
        tampered.kind = EvidenceKind::Metric;
        tampered.key = "sidecar.digest_match";
        tampered.hasValue = true;
        tampered.value = 0.0;
        tampered.source = "metric_record:exp-ndvi-1:mh-ndvi-1-tampered";
        items.push_back( tampered );

        const GradeOutcome outcome = gradeNdvi( items );
        checkSingleFault( outcome.report, "art-sidecar", { "grader:metric-ambiguous" }, 85.0 );
        // The ambiguity cites BOTH contradictory records.
        const CriterionOutcome *sidecar = findCriterion( outcome.report, "art-sidecar" );
        REQUIRE( sidecar != nullptr );
        CHECK( sidecar->evidenceIds.size() == 2 );
    }
}

TEST_CASE( "teaching benchmark: grading the same NDVI evidence 10x seals a byte-stable report digest",
           "[grader][adapters][teaching][benchmark]" )
{
    std::string firstDigest;
    for ( int round = 0; round < 10; ++round ) {
        CAPTURE( round );
        // Documents and rubric are rebuilt every round: the digest must not
        // depend on shared mutable state either.
        const GradeOutcome outcome = gradeNdvi( ndviEvidenceItems() );
        REQUIRE( outcome.report.score == 100.0 );
        if ( round == 0 )
            firstDigest = outcome.report.digest;
        CHECK( outcome.report.digest == firstDigest );

        // The sealed digest verifies over the canonical body.
        GraderError err;
        CHECK( outcome.report.verifyDigest( err ) );
        CHECK( err.ok() );
    }
    CHECK_FALSE( firstDigest.empty() );
}
