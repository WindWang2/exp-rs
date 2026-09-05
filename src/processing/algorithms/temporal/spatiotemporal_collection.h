// src/processing/algorithms/temporal/spatiotemporal_collection.h
// Naming bridge for Platform 3.0 (goal §5): "SpatioTemporalCollection" is the
// platform-3.0 name of the temporal workspace's collection type. Same class,
// same descriptor v1 persistence, same fingerprints — the multimodal
// observation surface (modality / sensor / polarizations / band roles) lives
// on TemporalSceneRef plus the typed view in spatiotemporal_contracts.h.
//
// A separate class would fork collection identity (DataManager records,
// revision-aware fingerprints, lineage, agent tools) for no behavioral gain,
// so the generalization is additive by design.
#pragma once

#include "temporal_collection.h"
#include "spatiotemporal_contracts.h"

namespace sicnu::temporal
{

using SpatioTemporalCollection = TemporalCollection;

} // namespace sicnu::temporal
