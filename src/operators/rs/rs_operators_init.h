#pragma once

namespace sicnu::operators::rs {

/// Registers every built-in rs:/gdal: operator with RSOperatorRegistry AND
/// wires the AtomicAlgorithmRegistry provider so `initialize()` resolves all
/// native algorithm ids. Idempotent; safe to call multiple times.
/// Production entry points call this once at startup; tests that exercise
/// `RSOperatorRegistry::create()` or `AtomicAlgorithmRegistry::findAdapter()`
/// for rs:* / gdal:* ids must call it explicitly — static registration from
/// a shared library is only guaranteed for symbols the binary references.
void initBuiltinRsOperators();

/// Installs the RS-operator → AtomicAlgorithmRegistry provider. Only safe to
/// call AFTER RSOperatorRegistry::instance()'s call_once chain has completed
/// (i.e. after initBuiltinRsOperators() has run to completion). Tests that
/// resolve rs:* / gdal:* ids via findAdapter() must call this plus
/// AtomicAlgorithmRegistry::initialize(); production entry points call it
/// once at startup. Calling it from inside instance()'s call_once deadlocks
/// (#707).
void installRsOperatorProvider();

} // namespace sicnu::operators::rs
