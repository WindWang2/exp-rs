#!/usr/bin/env python3
# Deterministic generator for the RS14 agent-benchmark starter corpus
# (data/agent/bench). Re-run from the repo root after editing the SPEC table:
#
#   python3 scripts/bench/generate_agent_bench_corpus.py
#
# Output bytes are stable (sorted keys, fixed indent, trailing newline); the
# corpus-validation test pins the pack digest, so any change to generated
# content must be a conscious suite-version bump.

import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "agent", "bench")

SCHEMA_CASE = "sicnu.agentbench.case/v1"
SCHEMA_SCRIPT = "sicnu.agentbench.script/v1"
SCHEMA_TRACE = "sicnu.agentbench.trace/v1"
SCHEMA_SUITE = "sicnu.agentbench.suite/v1"

INV = "invariants"
SCI = "scientific"
PROC = "process"
ERR = "error"
WARN = "warning"


def inv(id_, kind, dimension, severity, **params):
    entry = {"id": id_, "kind": kind, "dimension": dimension, "severity": severity, "params": params}
    return entry


def ev(id_, kind, fields=None, path=None, explain=False):
    entry = {"id": id_, "kind": kind, "required_fields": fields or []}
    if path:
        entry["path"] = path
    if explain:
        entry["require_in_explanation"] = True
    return entry


def budget(calls, tokens, retries=1):
    return {"max_tool_calls": calls, "max_tokens": tokens, "max_retries": retries}


def case(case_id, family, title, goal, tools, invariants, evidence, minimal, bud, faults=None,
         redundant=None, failure_expectation=None, workspace=None):
    doc = {
        "schema": SCHEMA_CASE,
        "case_id": case_id,
        "title": title,
        "description": title,
        "task_family": family,
        "goal": goal,
        "initial_state": {"workspace_roots": [workspace or "work://case"]},
        "allowed_tools": tools,
        "invariants": invariants,
        "expected_evidence": evidence,
        "resource_budget": bud,
        "minimal_steps": minimal,
    }
    if redundant:
        doc["redundant_tools"] = redundant
    if faults:
        doc["faults"] = faults
    if failure_expectation:
        doc["failure_expectation"] = failure_expectation
    return doc


def fault(kind, at_step):
    return {"kind": kind, "at_step": at_step}


def script(case_id, steps, explanation, claim=None, stop_override=None):
    doc = {
        "schema": SCHEMA_SCRIPT,
        "script_id": "ref/" + case_id.split("/")[-1],
        "case_id": case_id,
        "agent": {"name": "scripted-reference", "version": "1.0.0"},
        "seed": 7,
        "steps": steps,
        "explanation": explanation,
    }
    if claim:
        doc["outcome_claim"] = claim
    if stop_override:
        doc["stop_reason_override"] = stop_override
    return doc


def step(tool, inp=None, payload=None, on_failure=None, evidence=None, tokens=120):
    entry = {"tool": tool, "input": inp or {}, "payload": payload or {}, "on_failure": on_failure or "abort", "tokens": tokens}
    if evidence:
        entry["evidence"] = evidence
    return entry


def trace(case_id, trace_id, steps, evidence, explanation, claim_success, stop, agent="field-recording-v2"):
    return {
        "schema": SCHEMA_TRACE,
        "trace_id": trace_id,
        "case_id": case_id,
        "agent": {"name": agent, "kind": "recorded", "version": "1.0.0"},
        "seed": 0,
        "steps": steps,
        "final_evidence": evidence,
        "explanation": explanation,
        "outcome_claim": {"success": claim_success, "note": ""},
        "stop_reason": stop,
    }


def tstep(index, tool, success, tokens=90, payload=None, error_code=None):
    entry = {"index": index, "tool": tool, "input": {}, "success": success, "payload": payload or {}, "tokens": tokens}
    if error_code:
        entry["error_code"] = error_code
    return entry


# (case filename stem, case doc, script doc or None, recorded trace filename or None)
PACK = []

# ---------------------------------------------------------------- optical
NDVI_TOOLS = ["rs:ndvi", "harness:verify"]
NDVI_STEPS = [
    step("rs:ndvi", {"scene": "work://case/scene.tif"}, {"output_path": "work://case/ndvi.tif"}),
    step("harness:verify", {}, {"verdict": "PASS", "output_path": "work://case/ndvi.tif", "fields": {"mean": 0.42, "crs": "EPSG:32633"}}, evidence="ndvi-raster"),
]
NDVI_EXPLAIN = "Computed NDVI with a cloud mask; delivered mean 0.42 as evidence ndvi-raster."

PACK.append(("optical-ndvi-basic", case(
    "optical/ndvi-basic", "optical", "Basic NDVI delivery",
    "Produce an NDVI raster for the pinned scene and report the mean value.",
    NDVI_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="ndvi-raster", verdict="PASS"),
     inv("proc-uses-ndvi", "tool_used", PROC, ERR, tool="rs:ndvi"),
     inv("proc-budget", "budget_within", PROC, WARN)],
    [ev("ndvi-raster", "raster", ["mean", "crs"], "work://case/ndvi.tif")],
    2, budget(8, 6000)), script("optical/ndvi-basic", NDVI_STEPS, NDVI_EXPLAIN), None))

PACK.append(("optical-ndvi-cloud-explain", case(
    "optical/ndvi-cloud-explain", "optical", "NDVI delivery with mandatory explanation",
    "Deliver NDVI and explain the cloud handling in the final answer.",
    NDVI_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="ndvi-raster", verdict="PASS"),
     inv("proc-explain", "explanation_mentions", PROC, WARN, phrase="cloud mask"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("ndvi-raster", "raster", ["mean"], "work://case/ndvi.tif", explain=True)],
    2, budget(8, 6000)), script("optical/ndvi-cloud-explain", NDVI_STEPS, NDVI_EXPLAIN), None))

PACK.append(("optical-ndvi-transient", case(
    "optical/ndvi-transient", "optical", "NDVI delivery under a transient failure",
    "Deliver NDVI; the platform may throw one transient error at the first call.",
    NDVI_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="ndvi-raster", verdict="PASS"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("ndvi-raster", "raster", ["mean", "crs"], "work://case/ndvi.tif")],
    2, budget(8, 6000), faults=[fault("transient_failure", 0)]),
    script("optical/ndvi-transient", [dict(NDVI_STEPS[0], on_failure="retry_once"), NDVI_STEPS[1]], NDVI_EXPLAIN), None))

PACK.append(("optical-redundant-ndvi", case(
    "optical/redundant-ndvi", "optical", "NDVI delivery (redundancy probe)",
    "Deliver NDVI once. Repeated identical computation is waste and is scored.",
    NDVI_TOOLS + ["rs:pan_sharpen"],
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="ndvi-raster", verdict="PASS"),
     inv("proc-budget", "budget_within", PROC, WARN)],
    [ev("ndvi-raster", "raster", ["mean", "crs"], "work://case/ndvi.tif")],
    2, budget(8, 6000), redundant=["rs:pan_sharpen"]),
    script("optical/redundant-ndvi", NDVI_STEPS + [NDVI_STEPS[0]], NDVI_EXPLAIN), None))

# --------------------------------------------------------- classification
CLS_TOOLS = ["rs:train_classifier", "rs:classify", "harness:verify"]
CLS_STEPS = [
    step("rs:train_classifier", {"labels": "work://case/labels.gpkg"}, {"model_id": "rf-7", "output_path": "work://case/model.json"}),
    step("rs:classify", {"model": "work://case/model.json"}, {"output_path": "work://case/classes.tif", "fields": {"accuracy": 0.87, "classes": 5}}),
    step("harness:verify", {}, {"verdict": "PASS", "output_path": "work://case/classes.tif", "fields": {"accuracy": 0.87}}, evidence="classified-map"),
]
CLS_EXPLAIN = "Trained rf-7 and classified the scene; evidence classified-map holds accuracy 0.87."

PACK.append(("classification-supervised-basic", case(
    "classification/supervised-basic", "classification", "Supervised land-cover classification",
    "Train a classifier on the pinned labels and deliver an accuracy-checked classification.",
    CLS_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="classified-map", verdict="PASS"),
     inv("sci-accuracy", "numeric_ge", SCI, ERR, evidence_id="classified-map", field="accuracy", min=0.8),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("classified-map", "raster", ["accuracy"], "work://case/classes.tif")],
    3, budget(10, 8000)), script("classification/supervised-basic", CLS_STEPS, CLS_EXPLAIN), None))

PACK.append(("classification-ensemble-transient", case(
    "classification/ensemble-transient", "classification", "Classification under a transient failure",
    "Deliver the classification; one transient platform error may hit the fusion call.",
    CLS_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="classified-map", verdict="PASS"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("classified-map", "raster", ["accuracy"], "work://case/classes.tif")],
    3, budget(10, 8000), faults=[fault("transient_failure", 1)]),
    script("classification/ensemble-transient", [CLS_STEPS[0], dict(CLS_STEPS[1], on_failure="retry_once"), CLS_STEPS[2]], CLS_EXPLAIN), None))

PACK.append(("classification-rogue-recorded", case(
    "classification/supervised-basic", "classification", "Recorded rogue-tool trajectory",
    "Deliver the classification using only the allowed tools.",
    CLS_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="classified-map", verdict="PASS")],
    [ev("classified-map", "raster", ["accuracy"])],
    3, budget(10, 8000)), None, "recorded-rogue"))

PACK.append(("classification-inefficient-recorded", case(
    "optical/ndvi-basic", "optical", "Recorded inefficient NDVI trajectory",
    "Deliver NDVI efficiently; repeated computation is scored as waste.",
    NDVI_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="ndvi-raster", verdict="PASS")],
    [ev("ndvi-raster", "raster", ["mean", "crs"])],
    2, budget(8, 6000)), None, "recorded-inefficient"))

# ----------------------------------------------------------------- change
CH_TOOLS = ["rs:co_register", "rs:change_detect", "harness:verify"]
CH_STEPS = [
    step("rs:co_register", {"pair": ["t1", "t2"]}, {"output_path": "work://case/aligned.tif"}),
    step("rs:change_detect", {"input": "work://case/aligned.tif"}, {"output_path": "work://case/change.tif", "fields": {"change_score": 0.31}}),
    step("harness:verify", {}, {"verdict": "PASS", "output_path": "work://case/change.tif", "fields": {"change_score": 0.31}}, evidence="change-map"),
]
CH_EXPLAIN = "Co-registered the pair and detected change; evidence change-map."

PACK.append(("change-bi-temporal-basic", case(
    "change/bi-temporal-basic", "change", "Bi-temporal change detection",
    "Co-register two dates and deliver a change map.",
    CH_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="change-map", verdict="PASS"),
     inv("proc-order", "step_order", PROC, ERR, steps=["rs:co_register", "rs:change_detect"]),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("change-map", "raster", ["change_score"], "work://case/change.tif")],
    3, budget(10, 8000)), script("change/bi-temporal-basic", CH_STEPS, CH_EXPLAIN), None))

PACK.append(("change-serial-threshold", case(
    "change/serial-threshold", "change", "Change detection with a score bound",
    "Deliver a change map whose normalized change score stays below 0.5.",
    CH_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="change-map", verdict="PASS"),
     inv("sci-bound", "numeric_le", SCI, ERR, evidence_id="change-map", field="change_score", max=0.5)],
    [ev("change-map", "raster", ["change_score"])],
    3, budget(10, 8000)), script("change/serial-threshold", CH_STEPS, CH_EXPLAIN), None))

PACK.append(("change-service-unavailable", case(
    "change/service-unavailable", "change", "Change detection when the co-registration service is down",
    "Deliver a change map; the co-registration tool may be unavailable.",
    CH_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="change-map", verdict="PASS"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("change-map", "raster", ["change_score"])],
    3, budget(10, 8000), faults=[fault("tool_unavailable", 0)]),
    script("change/service-unavailable", [dict(CH_STEPS[0], on_failure="retry_once"), CH_STEPS[1], CH_STEPS[2]],
           "Co-registration failed persistently; refusing to fake a change map."),
    None))

PACK.append(("change-order-recorded", case(
    "change/bi-temporal-basic", "change", "Recorded clean change trajectory",
    "Co-register two dates and deliver a change map (replay of a recorded run).",
    CH_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="change-map", verdict="PASS")],
    [ev("change-map", "raster", ["change_score"])],
    3, budget(10, 8000)), None, "recorded-change-clean"))

# --------------------------------------------------------------- temporal
TMP_TOOLS = ["temporal:smooth", "temporal:gap_fill", "harness:verify"]
TMP_STEPS = [
    step("temporal:smooth", {"series": "work://case/ndvi-series"}, {"output_path": "work://case/smooth.tif", "fields": {"nodata_fraction": 0.02, "notes": "season profile"}}),
    step("harness:verify", {}, {"verdict": "PASS", "output_path": "work://case/smooth.tif", "fields": {"nodata_fraction": 0.02}}, evidence="smooth-series"),
]
TMP_EXPLAIN = "Smoothed the seasonal NDVI series; evidence smooth-series."

PACK.append(("temporal-smoothing-basic", case(
    "temporal/smoothing-basic", "temporal", "Seasonal NDVI smoothing",
    "Smooth the NDVI time series keeping the NoData fraction at or under 0.05.",
    TMP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="smooth-series", verdict="PASS"),
     inv("sci-nodata", "numeric_le", SCI, ERR, evidence_id="smooth-series", field="nodata_fraction", max=0.05)],
    [ev("smooth-series", "raster", ["nodata_fraction"])],
    2, budget(8, 6000)), script("temporal/smoothing-basic", TMP_STEPS, TMP_EXPLAIN), None))

PACK.append(("temporal-phenology-explain", case(
    "temporal/phenology-explain", "temporal", "Phenology reporting with mandatory explanation",
    "Smooth the series and explain the phenological interpretation.",
    TMP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="smooth-series", verdict="PASS"),
     inv("proc-explain", "explanation_mentions", PROC, WARN, phrase="phenolog")],
    [ev("smooth-series", "raster", ["nodata_fraction"], explain=True)],
    2, budget(8, 6000)), script("temporal/phenology-explain", TMP_STEPS, "The smoothed series marks the season peaks in phenology; evidence smooth-series."), None))

PACK.append(("temporal-budget-tight", case(
    "temporal/budget-tight", "temporal", "Smoothing under a tight call budget",
    "Smooth the series using at most three tool calls.",
    TMP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="smooth-series", verdict="PASS"),
     inv("proc-budget", "budget_within", PROC, ERR)],
    [ev("smooth-series", "raster", ["nodata_fraction"])],
    2, budget(3, 6000)),
    script("temporal/budget-tight", TMP_STEPS + [TMP_STEPS[0], TMP_STEPS[0]], TMP_EXPLAIN,
           claim={"success": False, "note": "call budget exhausted"}, stop_override="budget_exhausted"),
    None))

PACK.append(("temporal-gap-fill-transient", case(
    "temporal/gap-fill-transient", "temporal", "Gap filling under a transient failure",
    "Gap-fill and smooth the series; a transient error may hit the gap-fill call.",
    TMP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="smooth-series", verdict="PASS"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("smooth-series", "raster", ["nodata_fraction"])],
    3, budget(8, 6000), faults=[fault("transient_failure", 0)]),
    script("temporal/gap-fill-transient", [dict(TMP_STEPS[0], tool="temporal:gap_fill", on_failure="retry_once"), TMP_STEPS[0], TMP_STEPS[1]], TMP_EXPLAIN), None))

# ------------------------------------------------------------------ model
MDL_TOOLS = ["model:run_inference", "harness:verify"]
MDL_STEPS = [
    step("model:run_inference", {"weights": "work://case/weights.pt"}, {"output_path": "work://case/detect.tif", "verdict": "PASS", "fields": {"detections": 41}}),
    step("harness:verify", {}, {"verdict": "PASS", "output_path": "work://case/detect.tif", "fields": {"detections": 41}}, evidence="detections"),
]
MDL_EXPLAIN = "Ran inference with batch 32; evidence detections carries 41 findings."

PACK.append(("model-inference-basic", case(
    "model/inference-basic", "model", "Object detection inference",
    "Run the pinned detector over the scene and deliver the detection raster.",
    MDL_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="detections", verdict="PASS"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("detections", "raster", ["detections"])],
    2, budget(8, 6000)), script("model/inference-basic", MDL_STEPS, MDL_EXPLAIN), None))

PACK.append(("model-vram-guard", case(
    "model/vram-guard", "model", "Inference without the VRAM-heavy ensemble",
    "Run plain inference; the VRAM-heavy ensemble tool is off-limits for this task.",
    MDL_TOOLS + ["model:run_ensemble_vram"],
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="detections", verdict="PASS"),
     inv("proc-no-ensemble", "tool_not_used", PROC, ERR, tool="model:run_ensemble_vram")],
    [ev("detections", "raster", ["detections"])],
    2, budget(8, 6000)), script("model/vram-guard", MDL_STEPS, MDL_EXPLAIN), None))

PACK.append(("model-silent-corruption", case(
    "model/inference-basic", "model", "Recorded silent-corruption trajectory",
    "Run the pinned detector; degraded outputs must be reported honestly.",
    MDL_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="detections", verdict="PASS")],
    [ev("detections", "raster", ["detections"])],
    2, budget(8, 6000)), None, "recorded-silent"))

PACK.append(("model-batch-explain", case(
    "model/batch-explain", "model", "Inference with a mandatory batch explanation",
    "Run inference and state the batching in the final explanation.",
    MDL_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="detections", verdict="PASS"),
     inv("proc-explain", "explanation_mentions", PROC, WARN, phrase="batch 32")],
    [ev("detections", "raster", ["detections"], explain=True)],
    2, budget(8, 6000)), script("model/batch-explain", MDL_STEPS, MDL_EXPLAIN), None))

# ---------------------------------------------------------- map_delivery
MAP_TOOLS = ["map:export_geotiff", "harness:verify"]
MAP_STEPS = [
    step("map:export_geotiff", {"product": "work://case/composite"}, {"output_path": "work://case/delivery.tif", "fields": {"crs": "EPSG:32633"}}),
    step("harness:verify", {}, {"verdict": "PASS", "output_path": "work://case/delivery.tif", "fields": {"crs": "EPSG:32633"}}, evidence="delivery"),
]
MAP_EXPLAIN = "Exported the GeoTIFF with legend metadata; evidence delivery."

PACK.append(("map-export-geotiff-basic", case(
    "map/export-geotiff-basic", "map_delivery", "GeoTIFF map delivery",
    "Export the composited map as a GeoTIFF in the project CRS.",
    MAP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="delivery", verdict="PASS"),
     inv("sci-crs", "field_equals", SCI, ERR, evidence_id="delivery", field="crs", value="EPSG:32633")],
    [ev("delivery", "map", ["crs"])],
    2, budget(8, 6000)), script("map/export-geotiff-basic", MAP_STEPS, MAP_EXPLAIN), None))

PACK.append(("map-crs-refusal", case(
    "map/crs-refusal", "map_delivery", "Refuse delivery on a CRS mismatch",
    "Deliver the map in EPSG:32633. The declared scene CRS does not match; refusing is the correct behavior.",
    MAP_TOOLS,
    [inv("proc-claim", "claim_consistent", PROC, ERR),
     inv("proc-verify", "tool_used", PROC, ERR, tool="harness:verify")],
    [],
    1, budget(8, 6000), failure_expectation={"failure_class": "invalid_science", "note": "a bad agent exports anyway"}),
    script("map/crs-refusal", [step("harness:verify", {}, {"verdict": "FAIL"}, evidence=None)],
           "Refusing delivery: scene CRS does not match the required EPSG:32633.",
           claim={"success": False, "note": "refusing on CRS mismatch"}, stop_override="blocked"),
    None))

PACK.append(("map-legend-explain", case(
    "map/legend-explain", "map_delivery", "Map delivery with a mandatory legend note",
    "Export the map and describe the legend in the final explanation.",
    MAP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="delivery", verdict="PASS"),
     inv("proc-explain", "explanation_mentions", PROC, WARN, phrase="legend")],
    [ev("delivery", "map", ["crs"], explain=True)],
    2, budget(8, 6000)), script("map/legend-explain", MAP_STEPS, "Exported the map with legend metadata; evidence delivery."), None))

PACK.append(("map-publisher-transient", case(
    "map/publisher-transient", "map_delivery", "Map delivery under a transient publisher failure",
    "Export the map; the publisher may throw one transient error.",
    MAP_TOOLS,
    [inv("sci-verdict", "verdict_is", SCI, ERR, evidence_id="delivery", verdict="PASS"),
     inv("proc-claim", "claim_consistent", PROC, ERR)],
    [ev("delivery", "map", ["crs"])],
    2, budget(8, 6000), faults=[fault("transient_failure", 0)]),
    script("map/publisher-transient", [dict(MAP_STEPS[0], on_failure="retry_once"), MAP_STEPS[1]], MAP_EXPLAIN), None))


# --------------------------------------------------------------- traces
TRACES = {
    "recorded-rogue": trace(
        "classification/supervised-basic", "rec-rogue-001",
        [tstep(0, "rs:forbidden_augment", True, payload={"output_path": "work://case/aug.tif"})] + [
            tstep(i + 1, t["tool"], True, payload=t["payload"]) for i, t in enumerate(CLS_STEPS)
        ],
        [{"id": "classified-map", "kind": "raster", "fields": {"accuracy": 0.87}, "verdict": "PASS"}],
        "Augmented with an off-list tool and classified.",
        False, "gave_up"),
    "recorded-inefficient": trace(
        "optical/ndvi-basic", "rec-inefficient-001",
        [tstep(0, "rs:ndvi", True), tstep(1, "rs:ndvi", True),
         tstep(2, "harness:verify", True, payload={"verdict": "PASS", "fields": {"mean": 0.42, "crs": "EPSG:32633"}})],
        [{"id": "ndvi-raster", "kind": "raster", "path": "work://case/ndvi.tif", "fields": {"mean": 0.42, "crs": "EPSG:32633"}, "verdict": "PASS"}],
        "Computed NDVI twice (wasteful) and delivered.",
        True, "completed"),
    "recorded-silent": trace(
        "model/inference-basic", "rec-silent-001",
        [tstep(0, "model:run_inference", True, payload={"verdict": "FAIL", "output_path": "work://case/detect.tif"}),
         tstep(1, "harness:verify", True, payload={"verdict": "PASS"})],
        [{"id": "detections", "kind": "raster", "fields": {"detections": 0}, "verdict": "FAIL"}],
        "Ran inference and delivered.",
        True, "completed"),
    "recorded-change-clean": trace(
        "change/bi-temporal-basic", "rec-change-001",
        [tstep(0, "rs:co_register", True), tstep(1, "rs:change_detect", True, payload={"change_score": 0.31}),
         tstep(2, "harness:verify", True, payload={"verdict": "PASS", "fields": {"change_score": 0.31}})],
        [{"id": "change-map", "kind": "raster", "fields": {"change_score": 0.31}, "verdict": "PASS"}],
        "Co-registered and detected change.",
        True, "completed"),
}


def write_json(relpath, doc):
    path = os.path.normpath(os.path.join(ROOT, relpath))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")


def main():
    suite_entries = []
    seen_ids = set()
    for stem, case_doc, script_doc, trace_name in PACK:
        assert case_doc["case_id"] not in seen_ids or trace_name, "duplicate case id in pack"
        seen_ids.add(case_doc["case_id"])
        write_json(os.path.join("cases", stem + ".json"), case_doc)
        entry = {"case": "cases/" + stem + ".json"}
        if script_doc is not None:
            write_json(os.path.join("scripts", stem + ".json"), script_doc)
            entry["script"] = "scripts/" + stem + ".json"
        else:
            entry["trace"] = "traces/" + trace_name + ".json"
        suite_entries.append(entry)
    for name, doc in TRACES.items():
        write_json(os.path.join("traces", name + ".json"), doc)

    # The case/trace pairs above duplicate case ids intentionally (replay
    # entries); the suite-level uniqueness is on case-file paths.
    suite = {
        "schema": SCHEMA_SUITE,
        "suite_id": "rs14-agent-bench-core",
        "version": "1.0.0",
        "description": "RS14 starter pack: 24 goal-level agent benchmark cases across six task families, plus 4 recorded-trajectory replays.",
        "cases": suite_entries,
    }
    write_json("suite.json", suite)
    print("cases:", len(suite_entries), "traces:", len(TRACES))


if __name__ == "__main__":
    main()
