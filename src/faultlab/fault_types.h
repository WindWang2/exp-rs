// fault_types.h — value objects and typed results for the Scientific Fault
// Injection Lab Framework (RS14-13).
//
// The framework models a fault scenario as data: a base fixture, a fault
// transform with a seed, sandbox rules, expected observables, an expected
// diagnosis and a learning objective. This header owns the shared value
// objects; it deliberately holds no logic and no dependency beyond jsoncpp
// so the semantics stay testable in a pure C++ unit test (no Qt, no GDAL).
#pragma once

#include <json/json.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::faultlab
{

/// Schema id of the scenario document. The loader fails closed on anything
/// else — a newer document is never silently downgraded to this vocabulary.
inline constexpr const char *kFaultScenarioSchemaId = "sicnu.lab.faults/1";

/// Hard ceiling for one scenario's fixture bytes (64 MiB), enforced at load
/// time. Fault labs are teaching-scale; anything larger is a different tool.
inline constexpr std::uint64_t kFaultLabMaxBytes = 64ull * 1024 * 1024;

/// Canonical report schema id produced by the runner.
inline constexpr const char *kFaultReportSchemaId = "sicnu.faultlab.report/1";

enum class FaultSeverity
{
    Info,
    Warning,
    Error,
};

/// Typed, machine-readable diagnostic. Codes are stable identifiers in the
/// `faultlab.*` namespace — there is deliberately no silent fallback: an
/// unknown/unsafe/unsupported situation always arrives as one of these.
struct FaultDiagnostic
{
    std::string code;
    std::string message;
    FaultSeverity severity = FaultSeverity::Error;

    Json::Value toJson() const;
};

/// Result wrapper in the shape used across the science core
/// (src/data/data_result.h): ok flag + typed diagnostics, never exceptions
/// across the API boundary.
template <typename T>
struct FaultResult
{
    bool ok = false;
    T value{};
    std::vector<FaultDiagnostic> diagnostics;

    explicit operator bool() const
    {
        return ok;
    }

    T &operator*()
    {
        return value;
    }

    const T &operator*() const
    {
        return value;
    }

    T *operator->()
    {
        return &value;
    }

    const T *operator->() const
    {
        return &value;
    }

    const FaultDiagnostic *firstError() const
    {
        for ( const auto &diagnostic : diagnostics )
        {
            if ( diagnostic.severity == FaultSeverity::Error )
            {
                return &diagnostic;
            }
        }
        return nullptr;
    }
};

template <typename T>
FaultResult<T> makeOk( T value )
{
    FaultResult<T> result;
    result.ok = true;
    result.value = std::move( value );
    return result;
}

template <typename T>
FaultResult<T> makeError( std::string code, std::string message )
{
    FaultResult<T> result;
    result.ok = false;
    result.diagnostics.push_back( FaultDiagnostic{ std::move( code ), std::move( message ),
                                                   FaultSeverity::Error } );
    return result;
}

/// One raster band of a fixture. `samples` is row-major with width*height
/// entries; NaN marks a no-data sample so "NoData-as-data" faults have an
/// exact, measurable target. `role` uses the ADR-0065 lowercase vocabulary
/// ("red", "nir", "qa", "scene_classification", ...).
struct BandSpec
{
    std::string role;
    std::vector<double> samples;
    double scale = 1.0;
    double offset = 0.0;
    /// ISO-8601 date for temporal fixtures (empty for non-temporal bands).
    std::string acquisitionDate;

    Json::Value toJson() const;
};

/// Raster-agnostic fixture value object: the thing faults are applied to.
/// Grid semantics mirror ADR-0066 (`RasterGrid`): CRS identity, affine
/// geoTransform, per-band NoData. `extras` carries domain payloads that are
/// not pixels (train/test sample points, model channel order + weights,
/// provenance block) as JSON so scenarios stay declarative.
struct FaultGrid
{
    int width = 0;
    int height = 0;
    std::string crsId = "EPSG:4326";
    /// GDAL affine order: originX, pixelX, rotX, originY, rotY, pixelY.
    double geoTransform[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    bool hasNoData = false;
    double noDataValue = -9999.0;
    std::vector<BandSpec> bands;
    Json::Value extras{ Json::objectValue };

    std::size_t sampleCount() const
    {
        return static_cast<std::size_t>( width ) * static_cast<std::size_t>( height );
    }

    /// Approximate byte footprint of the sample data (budget accounting).
    std::uint64_t sampleBytes() const
    {
        return static_cast<std::uint64_t>( bands.size() ) * sampleCount() * sizeof( double );
    }

    /// First band index carrying `role`, or -1 when the role is absent.
    int bandIndexByRole( const std::string &role ) const;

    Json::Value toJson() const;
};

/// A measured fact about a (clean or faulted) fixture. Number/Text/Truth
/// keep the expectation language small and machine-checkable.
enum class ObservableKind
{
    Number,
    Text,
    Truth,
};

struct Observable
{
    std::string id;
    ObservableKind kind = ObservableKind::Number;
    double number = 0.0;
    std::string text;
    bool truth = false;

    Json::Value toJson() const;
};

/// Observable set keyed by observable id; std::map ordering keeps canonical
/// serialization stable.
using ObservableSet = std::map<std::string, Observable>;

enum class ExpectationRelation
{
    Equals,
    NotEquals,
    Changed,
    DeltaGe,
    DeltaLe,
    InRange,
    TruthIs,
};

const char *expectationRelationName( ExpectationRelation relation );
bool parseExpectationRelation( const std::string &text, ExpectationRelation &out );

/// One declared expectation. For number observables `value` is the relation
/// operand (Equals/DeltaGe/DeltaLe/TruthIs); InRange uses rangeLo/rangeHi.
/// Text observables support Equals/NotEquals against `text`; the relation
/// operand for those travels in `value` as well (see loader: text
/// expectations carry `text` in the document).
struct ObservableExpectation
{
    std::string id;
    ExpectationRelation relation = ExpectationRelation::Changed;
    double value = 0.0;
    double rangeLo = 0.0;
    double rangeHi = 0.0;
    std::string text;

    Json::Value toJson() const;
};

/// Evidence for one checked expectation — observable id, relation, expected
/// vs observed with delta, in the graded-failure style of the lab grader
/// (a failure is evidence, never a bare boolean).
struct ExpectationResult
{
    std::string id;
    ExpectationRelation relation = ExpectationRelation::Changed;
    bool passed = false;
    double expected = 0.0;
    double observed = 0.0;
    double delta = 0.0;
    std::string expectedText;
    std::string observedText;
    std::string note;

    Json::Value toJson() const;
};

/// A fault to inject: family id (closed vocabulary from the registry),
/// family-specific params (closed per-family param vocabulary, validated by
/// the transform), and the seed that drives every stochastic step.
struct FaultSpec
{
    std::string familyId;
    Json::Value params{ Json::objectValue };
    std::uint32_t seed = 0;

    Json::Value toJson() const;
};

/// A complete scenario document (`sicnu.lab.faults/1`).
struct FaultScenario
{
    std::string scenarioId;
    std::string title;
    std::string titleZh;

    FaultSpec fault;
    std::string fixtureId;
    Json::Value fixtureParams{ Json::objectValue };
    std::uint32_t fixtureSeed = 0;

    std::uint64_t maxBytes = kFaultLabMaxBytes;
    std::string sandboxClass = "temp_copy";

    std::vector<ObservableExpectation> expectations;
    std::string expectedDiagnosisSignature;
    /// Optional artifact-level integration: grading rules reference plus the
    /// assertion ids that must fire on the faulted artifact.
    Json::Value verifier{ Json::objectValue };

    std::string learningObjectiveId;
    std::string learningObjective;
    std::string learningObjectiveZh;

    Json::Value toJson() const;
};

/// Loads and validates a scenario document. Fails closed: foreign schema
/// versions, unknown families/relations, malformed fields — each maps to a
/// typed `faultlab.*` diagnostic.
FaultResult<FaultScenario> loadFaultScenario( const Json::Value &doc );
FaultResult<FaultScenario> loadFaultScenarioFile( const std::string &path );

} // namespace sicnu::faultlab
