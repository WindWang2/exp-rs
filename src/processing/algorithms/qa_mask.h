// src/processing/algorithms/qa_mask.h — quality/cloud/shadow/snow masking
#pragma once

#include <cstddef>
#include <cstdint>

/// QA-mask kernels: derive a binary mask (1 = masked/obscured, 0 = clear)
/// from a quality-assurance band — Landsat Collection 2 QA_PIXEL bits, the
/// Sentinel-2 Scene Classification Layer, or a generic bitmask.
namespace QaMask
{

/// Selectable Landsat Collection 2 QA_PIXEL bit flags.
enum LandsatMaskFlags : uint32_t {
    LandsatMaskNone = 0,
    LandsatMaskFill = 1u << 0,          ///< bit 0 — fill
    LandsatMaskDilatedCloud = 1u << 1,  ///< bit 1 — dilated cloud
    LandsatMaskCirrus = 1u << 2,        ///< bit 2 — cirrus
    LandsatMaskCloud = 1u << 3,         ///< bit 3 — cloud
    LandsatMaskCloudShadow = 1u << 4,   ///< bit 4 — cloud shadow
    LandsatMaskSnow = 1u << 5,          ///< bit 5 — snow
    LandsatMaskWater = 1u << 7,         ///< bit 7 — water
};

/// Sentinel-2 Scene Classification Layer class ids.
enum SclClass : uint8_t {
    SclNoData = 0,
    SclSaturated = 1,
    SclDarkFeatures = 2,   ///< dark features / terrain shadow
    SclCloudShadow = 3,
    SclVegetation = 4,
    SclNotVegetated = 5,
    SclWater = 6,
    SclUnclassified = 7,
    SclCloudMediumProbability = 8,
    SclCloudHighProbability = 9,
    SclThinCirrus = 10,
    SclSnow = 11,
};

/// Masks pixels whose Landsat QA_PIXEL value has any of @a flags set.
/// @param qa    uint16 QA_PIXEL values
/// @param mask  output buffer (count bytes), 1 where masked
void landsatQaMask(const uint16_t *qa, uint8_t *mask, size_t count, uint32_t flags);

/// Masks pixels whose SCL class is selected in @a classes (index = class id,
/// 16 entries). Classes outside 0..15 are treated as unselected.
/// @param scl     uint8 SCL values
/// @param mask    output buffer (count bytes), 1 where masked
void sclMask(const uint8_t *scl, uint8_t *mask, size_t count, const bool classes[16]);

/// Masks pixels where @a values has any of @a bits set (generic bitmask).
/// @param values  uint16 values
/// @param mask    output buffer (count bytes), 1 where masked
void genericBitmaskMask(const uint16_t *values, uint8_t *mask, size_t count, uint16_t bits);

} // namespace QaMask
