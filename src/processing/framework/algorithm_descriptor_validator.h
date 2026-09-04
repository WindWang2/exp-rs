#pragma once

#include "algorithm_descriptor.h"

#include <string>
#include <vector>

namespace sicnu::processing {

struct ValidationIssue {
  enum class Severity { Warning, Error };
  Severity severity = Severity::Error;
  std::string field;
  std::string message;
};

struct DescriptorValidationReport {
  std::string algorithmId;
  bool ok = true;
  std::vector<ValidationIssue> issues;

  void addError( const std::string &field, const std::string &msg ) {
    ok = false;
    issues.push_back( { ValidationIssue::Severity::Error, field, msg } );
  }

  void addWarning( const std::string &field, const std::string &msg ) {
    issues.push_back( { ValidationIssue::Severity::Warning, field, msg } );
  }
};

struct CatalogValidationReport {
  bool ok = true;
  std::vector<DescriptorValidationReport> operatorReports;
  std::vector<std::string> globalErrors;
};

class AlgorithmDescriptorValidator {
public:
  /// Validates a single AlgorithmDescriptor according to the platform contract.
  static DescriptorValidationReport validateDescriptor( const AlgorithmDescriptor &desc,
                                                        bool exposedToTaskCatalog = false );

  /// Validates an entire catalog of AlgorithmDescriptors, checking uniqueness and coherence.
  static CatalogValidationReport validateCatalog( const std::vector<AlgorithmDescriptor> &descriptors,
                                                  bool taskCatalogMode = false );

  /// Checks whether descriptor serialization is byte-reproducible (ADR 0124).
  static bool isDeterministic( const AlgorithmDescriptor &desc );
};

} // namespace sicnu::processing
