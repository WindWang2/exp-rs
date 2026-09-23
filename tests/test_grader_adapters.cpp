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
        doc["metrics"]["confusion_matrix"]["kappa"] = Json::Value( 1e999 );
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
