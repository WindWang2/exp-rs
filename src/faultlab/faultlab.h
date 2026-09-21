// faultlab.h — umbrella include for the Scientific Fault Injection Lab
// Framework (RS14-13). Consumers include this one header; the modules below
// can also be included individually. The umbrella grows as slices land
// (A core types → B/C/D/E fault families → F runner → G exemplars).
#pragma once

#include "deterministic.h"
#include "fault_expectations.h"
#include "fault_observables.h"
#include "fault_registry.h"
#include "fault_report.h"
#include "fault_sandbox.h"
#include "fault_transforms.h"
#include "fault_types.h"
#include "util/canonical_json.h"
#include "util/sha256.h"
