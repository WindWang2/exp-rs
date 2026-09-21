/***************************************************************************
  lab/sha256.h
  LabSpec v3 lab runtime — self-contained SHA-256 (FIPS 180-4).

  Why a local implementation: the lab runtime is a Qt-free metadata layer
  that links jsoncpp only (the layer guard is the test link graph), so it
  can neither use QCryptographicHash nor pull Sicnu::Geospatial for
  geospatial/util/sha256.h. Same public-domain-shape discipline as the
  geospatial utility; validated against the FIPS 180-4 known-answer vectors
  in tests/test_lab_runtime.cpp. Not for adversarial crypto use — digests
  here are identity/drift evidence, not a trust root.
***************************************************************************/

#ifndef SICNU_LAB_SHA256_H
#define SICNU_LAB_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sicnu::lab
{

/// One-shot SHA-256 over a byte range; returns 64 lowercase hex chars.
std::string sha256Hex( std::string_view bytes );

} // namespace sicnu::lab

#endif // SICNU_LAB_SHA256_H
