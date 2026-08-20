// src/processing/algorithms/qa_mask.cpp — QA mask kernels
#include "qa_mask.h"

namespace QaMask
{

void landsatQaMask(const uint16_t *qa, uint8_t *mask, size_t count, uint32_t flags)
{
    if (!qa || !mask)
        return;
    const uint16_t bits = static_cast<uint16_t>(flags & 0xFFFFu);
    for (size_t i = 0; i < count; ++i)
        mask[i] = (qa[i] & bits) != 0 ? 1 : 0;
}

void sclMask(const uint8_t *scl, uint8_t *mask, size_t count, const bool classes[16])
{
    if (!scl || !mask || !classes)
        return;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t cls = scl[i];
        mask[i] = (cls < 16 && classes[cls]) ? 1 : 0;
    }
}

void genericBitmaskMask(const uint16_t *values, uint8_t *mask, size_t count, uint16_t bits)
{
    if (!values || !mask)
        return;
    for (size_t i = 0; i < count; ++i)
        mask[i] = (values[i] & bits) != 0 ? 1 : 0;
}

} // namespace QaMask
