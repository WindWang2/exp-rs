// src/agent/autonomy/autonomy_holder.h
#pragma once

//
// RS14-12: the process-wide autonomy policy holder.
//
// The course/host installs the course-layer policy once; gate call sites
// resolve the effective policy from (course layer + session/labspec/teacher
// layers) at decision time. Same singleton pattern as LabSpecCatalog.
//
// Default course policy is the research default (L5, agent mode): the gate
// is present everywhere, but pre-existing research flows behave exactly as
// before until a teaching host installs a restrictive policy. The lab
// (teaching) domain is governed regardless — the structural lab-student
// execution rule in the decision engine does not depend on any policy.

#include <mutex>
#include <string>
#include <vector>

#include "agent/autonomy/autonomy_policy.h"

namespace sicnu::agent::autonomy {

class AutonomyPolicyHolder
{
  public:
    static AutonomyPolicyHolder &instance();

    /// Installs the course layer. Replaces any previous course policy.
    void installCoursePolicy( const AutonomyPolicy &policy );

    /// Parses and installs the course layer; a malformed document is refused
    /// (typed errors via problems()) and the previous policy is kept.
    bool installCoursePolicyJson( const std::string &text );

    /// The research default: L5 / agent. Behavior-compatible with flows that
    /// predate the autonomy layer.
    static AutonomyPolicy researchDefaultPolicy();

    AutonomyPolicy coursePolicy() const;

    /// Course layer + caller-supplied layers, resolved by the one merge rule.
    AutonomyPolicy effectivePolicy( const std::vector<AutonomyPolicyLayer> &extraLayers ) const;

    /// Typed parse problems from the last refused installCoursePolicyJson.
    std::vector<std::string> problems() const;

  private:
    AutonomyPolicyHolder() = default;

    mutable std::mutex mMutex;
    AutonomyPolicy mCoursePolicy = researchDefaultPolicy();
    std::vector<std::string> mProblems;
};

} // namespace sicnu::agent::autonomy
