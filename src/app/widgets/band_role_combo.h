// band_role_combo.h — shared semantic band-role selector (C5)
#pragma once

#include <QComboBox>

#include "data/band_role.h"

/**
 * Shared band-role selector used by product-aware dialogs (QA mask, spectral
 * index, ...): lists a raster's bands labeled with their semantic role
 * (SICNU_BAND_ROLE metadata, ADR 0065) plus an "自动（按产品语义角色）" item,
 * so workflows can offer role-based band picking instead of one-off per-dialog
 * combo logic. Band numbers remain available via selectedBand() as the
 * low-level fallback.
 */
class BandRoleCombo : public QComboBox
{
    Q_OBJECT

public:
    explicit BandRoleCombo( QWidget *parent = nullptr );

    /// Populate from a raster source. Item 0 is "自动" (data 0); then one
    /// item per band labeled "波段 N" (+ role display name when known).
    /// Clears to the empty state when the source cannot be opened.
    void setRaster( const QString &source );

    /// True when setRaster() populated a raster (at least the auto item).
    bool hasRaster() const { return m_hasRaster; }

    /// 1-based selected band, or 0 for the "自动" item / no raster.
    int selectedBand() const;

    /// Semantic role of the selected band (Unknown for auto / unknown role).
    sicnu::data::BandRole selectedRole() const;

    /// Preselect the first band carrying @p role; falls back to the "自动"
    /// item when none does. No-op without a raster.
    void selectBandByRole( sicnu::data::BandRole role );

private:
    bool m_hasRaster = false;
};
