#include "algorithm_descriptor_validator.h"

#include <algorithm>
#include <regex>
#include <set>
#include <unordered_set>

namespace sicnu::processing {

namespace {

const std::set<std::string> kKnownTaskFamilies = {
  "classification", "segmentation", "change-detection", "temporal",
  "spectral", "filter", "geometry", "index", "indices", "composite",
  "summary", "trend", "anomaly", "extraction", "inference", "fusion",
  "correction", "calibration", "resample", "io", "statistics", "analysis",
  "vectorization", "quality-masking", "index-computation", "rasterization"
};

const std::set<std::string> kValidModalities = {
  "optical", "sar", "dem", "elevation", "multimodal", "auxiliary",
  "vector", "raster", "thermal", "multispectral", "hyperspectral", "rgb", "any"
};

const std::set<std::string> kValidBandRoles = {
  "red", "green", "blue", "nir", "swir1", "swir2", "pan", "qa", "mask",
  "thermal", "vv", "vh", "hh", "hv", "co-pol", "cross-pol",
  "elevation", "slope", "aspect", "cost", "distance", "quality"
};

const std::set<std::string> kValidMemoryPolicies = {
  "streaming", "multipass_streaming", "full_raster", "external_process",
  "unsupported_for_large_raster"
};

std::string toLower( const std::string &s ) {
  std::string out = s;
  std::transform( out.begin(), out.end(), out.begin(), []( unsigned char c ) { return std::tolower( c ); } );
  return out;
}

} // namespace

DescriptorValidationReport AlgorithmDescriptorValidator::validateDescriptor( const AlgorithmDescriptor &desc,
                                                                             bool exposedToTaskCatalog )
{
  DescriptorValidationReport report;
  report.algorithmId = desc.id;

  // 1. Operator ID checks
  if ( desc.id.empty() )
  {
    report.addError( "id", "operator ID must not be empty" );
  }
  else
  {
    if ( desc.id.find( ':' ) == std::string::npos )
      report.addError( "id", "operator ID must include namespace prefix, e.g. 'rs:name'" );
    static const std::regex kIdRegex( "^[a-zA-Z0-9_\\-]+:[a-zA-Z0-9_\\-\\.]+$" );
    if ( !std::regex_match( desc.id, kIdRegex ) )
      report.addError( "id", "operator ID format invalid: " + desc.id );
  }

  // 2. Task family checks
  const std::string &tf = desc.agentMetadata.taskFamily;
  if ( exposedToTaskCatalog && tf.empty() )
  {
    report.addError( "agentMetadata.taskFamily",
                     "task family must be valid and non-empty when exposed to task catalog" );
  }
  else if ( !tf.empty() )
  {
    if ( kKnownTaskFamilies.find( toLower( tf ) ) == kKnownTaskFamilies.end() )
    {
      report.addError( "agentMetadata.taskFamily",
                       "unknown task family '" + tf + "'" );
    }
  }

  // 3. Port uniqueness & schema validation
  std::set<std::string> inputNames;
  for ( const auto &inPort : desc.inputs )
  {
    if ( inPort.name.empty() )
      report.addError( "inputs", "input port name cannot be empty" );
    else if ( !inputNames.insert( inPort.name ).second )
      report.addError( "inputs." + inPort.name, "duplicate input port name: " + inPort.name );

    if ( inPort.hasMinimum && inPort.hasMaximum && inPort.minimum > inPort.maximum )
      report.addError( "inputs." + inPort.name, "minimum cannot be greater than maximum" );

    if ( inPort.type == DataType::Enum && inPort.enumOptions.empty() )
      report.addError( "inputs." + inPort.name, "enum port has no options" );

    // Modality check
    if ( inPort.rsContract.isObject() )
    {
      if ( inPort.rsContract.isMember( "dataKind" ) && inPort.rsContract["dataKind"].isString() )
      {
        const std::string kind = toLower( inPort.rsContract["dataKind"].asString() );
        if ( kValidModalities.find( kind ) == kValidModalities.end() )
          report.addError( "inputs." + inPort.name + ".rsContract.dataKind", "invalid dataKind/modality: " + kind );
      }
      if ( inPort.rsContract.isMember( "modality" ) && inPort.rsContract["modality"].isString() )
      {
        const std::string mod = toLower( inPort.rsContract["modality"].asString() );
        if ( kValidModalities.find( mod ) == kValidModalities.end() )
          report.addError( "inputs." + inPort.name + ".rsContract.modality", "invalid modality: " + mod );
      }
      if ( inPort.rsContract.isMember( "modalities" ) && inPort.rsContract["modalities"].isArray() )
      {
        for ( const auto &m : inPort.rsContract["modalities"] )
        {
          if ( m.isString() )
          {
            const std::string mod = toLower( m.asString() );
            if ( kValidModalities.find( mod ) == kValidModalities.end() )
              report.addError( "inputs." + inPort.name + ".rsContract.modalities", "invalid modality: " + mod );
          }
        }
      }
      if ( inPort.rsContract.isMember( "band_roles" ) && inPort.rsContract["band_roles"].isArray() )
      {
        for ( const auto &br : inPort.rsContract["band_roles"] )
        {
          if ( br.isString() )
          {
            const std::string role = toLower( br.asString() );
            if ( kValidBandRoles.find( role ) == kValidBandRoles.end() )
              report.addError( "inputs." + inPort.name + ".rsContract.band_roles", "invalid band role: " + role );
          }
        }
      }
    }
  }

  std::set<std::string> outputNames;
  for ( const auto &outPort : desc.outputs )
  {
    if ( outPort.name.empty() )
      report.addError( "outputs", "output port name cannot be empty" );
    else if ( !outputNames.insert( outPort.name ).second )
      report.addError( "outputs." + outPort.name, "duplicate output port name: " + outPort.name );
  }

  // 4. Temporal consistency
  const bool isTemporalTask = ( tf == "temporal" || desc.id.find( "temporal" ) != std::string::npos );
  if ( isTemporalTask )
  {
    bool hasTemporalInput = false;
    for ( const auto &inPort : desc.inputs )
    {
      if ( inPort.name == "scenes" || inPort.name == "collection" || inPort.name == "before" || inPort.name == "after" )
      {
        hasTemporalInput = true;
        break;
      }
    }
    if ( !hasTemporalInput )
    {
      report.addWarning( "inputs", "temporal operator declared without 'scenes' or 'collection' input port" );
    }
  }

  // 5. GPU declaration coherence
  if ( desc.agentMetadata.gpuAccelerated && !desc.agentMetadata.gpuDeclared )
  {
    report.addError( "agentMetadata.gpuDeclared",
                     "gpuAccelerated is true but gpuDeclared was not set to true" );
  }

  // 6. Memory policy
  const std::string &mp = desc.agentMetadata.memoryPolicy;
  if ( !mp.empty() && kValidMemoryPolicies.find( mp ) == kValidMemoryPolicies.end() )
  {
    report.addError( "agentMetadata.memoryPolicy", "unknown memory policy '" + mp + "'" );
  }

  // 7. Deterministic declaration
  const std::string &dg = desc.agentMetadata.determinismGrade;
  if ( !dg.empty() && dg != "bit_exact" && dg != "tolerance" )
  {
    report.addError( "agentMetadata.determinismGrade", "determinismGrade must be 'bit_exact' or 'tolerance'" );
  }

  // 8. Cost class
  const std::string &cc = desc.agentMetadata.costClass;
  if ( !cc.empty() )
  {
    if ( cc.rfind( "O(", 0 ) != 0 )
      report.addError( "agentMetadata.costClass", "cost class must start with O(...) notation, got: " + cc );
  }

  // 9. Large raster safety
  if ( desc.agentMetadata.largeRasterSafe && mp == "unsupported_for_large_raster" )
  {
    report.addError( "agentMetadata.largeRasterSafe",
                     "largeRasterSafe is true but memoryPolicy is unsupported_for_large_raster" );
  }

  // 10. Model requirements for inference
  if ( tf == "inference" )
  {
    bool hasModelInput = false;
    for ( const auto &inPort : desc.inputs )
    {
      if ( inPort.name == "model" || inPort.name == "model_path" || inPort.name == "weights" || inPort.name == "onnx" )
      {
        hasModelInput = true;
        break;
      }
    }
    if ( !hasModelInput )
      report.addWarning( "inputs", "inference operator declared without explicit model input port" );
  }

  return report;
}

CatalogValidationReport AlgorithmDescriptorValidator::validateCatalog( const std::vector<AlgorithmDescriptor> &descriptors,
                                                                       bool taskCatalogMode )
{
  CatalogValidationReport catReport;
  std::unordered_set<std::string> seenIds;

  for ( const auto &desc : descriptors )
  {
    if ( !desc.id.empty() )
    {
      if ( !seenIds.insert( desc.id ).second )
      {
        catReport.ok = false;
        catReport.globalErrors.push_back( "Duplicate operator ID in catalog: " + desc.id );
      }
    }

    DescriptorValidationReport rep = validateDescriptor( desc, taskCatalogMode );
    if ( !rep.ok )
      catReport.ok = false;
    catReport.operatorReports.push_back( std::move( rep ) );
  }

  return catReport;
}

bool AlgorithmDescriptorValidator::isDeterministic( const AlgorithmDescriptor &desc )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  std::string s1 = Json::writeString( builder, desc.toToolCallDefinition() );
  std::string s2 = Json::writeString( builder, desc.toToolCallDefinition() );
  return ( s1 == s2 && !s1.empty() );
}

} // namespace sicnu::processing
