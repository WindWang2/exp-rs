/***************************************************************************
 * design_tokens.h — SICNU GEO RS UI design tokens (Canopy Lab) as C++ constants.
 *
 * Desktop Workbench UX 4.0 (Milestone F): one owner for semantic colors,
 * spacing, icon sizes and type sizes in src/app C++ code. The values mirror
 * the design-token header comment in resources/styles.qss (light) and
 * resources/styles-dark.qss (dark); tests/test_theme_selector_parity.cpp
 * asserts the two stay in sync, so change QSS and tokens together.
 *
 * Rule: new C++ code in src/app uses these tokens instead of local QColor
 * literals or hand-rolled dark/light switches.
 ***************************************************************************/
#pragma once

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QWidget>

namespace SicnuUi {
namespace Tokens {

// ---------------------------------------------------------------------------
// Theme detection (single owner — RsJobPanel/georef/pipeline used to each
// reimplement a lightness probe).
// ---------------------------------------------------------------------------

/// True when @a widget should render with the dark (Canopy Lab Night) values.
/// Falls back to the application palette when @a widget is null.
inline bool themeIsDark( const QWidget *widget )
{
    const QPalette pal = widget ? widget->palette()
                                : ( qApp ? qApp->palette() : QPalette() );
    return pal.color( QPalette::Window ).lightness() < 128;
}

// ---------------------------------------------------------------------------
// Light theme (Canopy Lab) — mirrors resources/styles.qss header tokens.
// ---------------------------------------------------------------------------

namespace Light {
inline constexpr QColor canvas        = QColor( 0xF4, 0xF6, 0xF8 ); // surface.canvas #F4F6F8
inline constexpr QColor panel         = QColor( 0xFF, 0xFF, 0xFF ); // surface.panel #FFFFFF
inline constexpr QColor raised        = QColor( 0xEE, 0xF1, 0xF4 ); // surface.raised #EEF1F4
inline constexpr QColor inkPrimary    = QColor( 0x1C, 0x24, 0x30 ); // ink.primary #1C2430
inline constexpr QColor inkSecondary  = QColor( 0x5A, 0x65, 0x73 ); // ink.secondary #5A6573
inline constexpr QColor lineSubtle    = QColor( 0xE2, 0xE6, 0xEB ); // line.subtle #E2E6EB
inline constexpr QColor lineStrong    = QColor( 0xC5, 0xCD, 0xD6 ); // line.strong #C5CDD6
inline constexpr QColor accent        = QColor( 0x0B, 0x6E, 0x4F ); // accent #0B6E4F
inline constexpr QColor accentHover   = QColor( 0x09, 0x5C, 0x42 ); // accent hover #095C42
inline constexpr QColor mapSelect     = QColor( 0x1B, 0x6C, 0xA8 ); // mapSelect #1B6CA8
inline constexpr QColor ok            = QColor( 0x1A, 0x7F, 0x37 ); // state.ok #1A7F37
inline constexpr QColor warn          = QColor( 0xB5, 0x81, 0x00 ); // state.warn #B58100
inline constexpr QColor err           = QColor( 0xC9, 0x37, 0x2C ); // state.err #C9372C
inline constexpr QColor ai            = QColor( 0x6E, 0x56, 0xCF ); // ai #6E56CF
inline constexpr QColor veg           = QColor( 0x2F, 0x9E, 0x44 ); // domain veg
inline constexpr QColor water         = QColor( 0x0B, 0x6B, 0xCB ); // domain water
inline constexpr QColor soil          = QColor( 0xA6, 0x7C, 0x52 ); // domain soil
} // namespace Light

// ---------------------------------------------------------------------------
// Dark theme (Canopy Lab Night) — mirrors resources/styles-dark.qss tokens.
// ---------------------------------------------------------------------------

namespace Dark {
inline constexpr QColor canvas        = QColor( 0x1A, 0x1D, 0x23 ); // #1A1D23
inline constexpr QColor panel         = QColor( 0x1E, 0x22, 0x29 ); // #1E2229
inline constexpr QColor raised        = QColor( 0x25, 0x2A, 0x33 ); // #252A33
inline constexpr QColor inkPrimary    = QColor( 0xE8, 0xEC, 0xF1 ); // #E8ECF1
inline constexpr QColor inkSecondary  = QColor( 0xA8, 0xB0, 0xBC ); // #A8B0BC
inline constexpr QColor lineSubtle    = QColor( 0x34, 0x3B, 0x46 );
inline constexpr QColor lineStrong    = QColor( 0x4A, 0x52, 0x60 );
inline constexpr QColor accent        = QColor( 0x2B, 0xB6, 0x73 ); // #2BB673
inline constexpr QColor accentHover   = QColor( 0x3D, 0xCF, 0x6A ); // #3DCF6A
inline constexpr QColor mapSelect     = QColor( 0x4D, 0xA3, 0xE0 ); // #4DA3E0
inline constexpr QColor ok            = QColor( 0x3D, 0xCF, 0x6A ); // #3DCF6A
inline constexpr QColor warn          = QColor( 0xE0, 0xA4, 0x58 ); // #E0A458
inline constexpr QColor err           = QColor( 0xF0, 0x71, 0x67 ); // #F07167
inline constexpr QColor ai            = QColor( 0x9D, 0x8C, 0xFF );
} // namespace Dark

// ---------------------------------------------------------------------------
// Task-status palette (one owner for job lists; UX 4.0 Milestone F2).
// ---------------------------------------------------------------------------

/// Semantic state colors shared by RsJobPanel, the georeferencer task list and
/// the pipeline editor node badges.
inline QColor statusOk( bool dark )     { return dark ? Dark::ok : Light::ok; }
inline QColor statusWarn( bool dark )   { return dark ? Dark::warn : Light::warn; }
inline QColor statusError( bool dark )  { return dark ? Dark::err : Light::err; }
inline QColor statusRunning( bool dark )  { return dark ? Dark::mapSelect : QColor( 0x09, 0x69, 0xDA ); }
inline QColor statusWaiting( bool dark )  { return dark ? QColor( 0xB0, 0x8A, 0xD0 ) : QColor( 0x6F, 0x42, 0xC1 ); }
inline QColor statusDispatching( bool dark ) { return dark ? QColor( 0x6F, 0xA8, 0xDC ) : QColor( 0x54, 0x8C, 0xD7 ); }
inline QColor statusCancelling( bool dark )  { return dark ? QColor( 0xD9, 0x7B, 0x0F ) : QColor( 0xBF, 0x68, 0x00 ); }
inline QColor statusIdle( bool dark )     { return dark ? Dark::inkSecondary : Light::inkSecondary; }

// ---------------------------------------------------------------------------
// Layout scale
// ---------------------------------------------------------------------------

/// 4-based spacing scale (px): spacing(0..5) → 0/4/8/12/16/24.
constexpr int spacing( int step )
{
    switch ( step )
    {
        case 0: return 0;
        case 1: return 4;
        case 2: return 8;
        case 3: return 12;
        case 4: return 16;
        default: return 24;
    }
}

/// Standard icon sizes (px). Fixed-size chrome that must follow High-DPI
/// should query devicePixelRatio instead of inventing new sizes.
constexpr int kIconToolbar   = 20;
constexpr int kIconPanelTree = 18;
constexpr int kIconInline    = 16;
constexpr int kIconEmptyState = 48;
constexpr int kIconEmptyStateHero = 64;

/// Type sizes (px) matching the QSS body/hint scale.
constexpr int kFontBody    = 13;
constexpr int kFontHint    = 12;
constexpr int kFontMicro   = 11;

/// Corner radius used across panels and dialogs.
constexpr int kRadius = 4;

} // namespace Tokens
} // namespace SicnuUi
