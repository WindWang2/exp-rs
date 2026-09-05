/***************************************************************************
 * exprs/plugin_ui.h — ExpRS UI plugin surface (interface version 1)
 *
 * SECURITY/ABI NOTE: this is the ONLY public SDK header that uses Qt types.
 * UI plugins are compiled against the same Qt 6 minor version and the same
 * application build as the host (they link qgis_core/qgis_gui anyway), so
 * they are build-locked, not load-anywhere binaries. The manifest's
 * abi_version gate still applies.
 *
 * Model: the host PULLS contributions. A UI plugin never touches the main
 * window or its private widgets — it creates widgets/actions when asked and
 * the host decides placement, object names and lifetime. Contribution
 * factories may be called again after releaseUi() (workspace rebuilds);
 * releaseUi() must leave the plugin owning nothing.
 ***************************************************************************/
#pragma once

#include <QAction>
#include <QList>
#include <QString>
#include <QWidget>

namespace exprs {

/// UI contribution interface: implemented by plugins declaring the "ui"
/// (or "cartography") capability and exporting the second entry point
/// EXPRS_createUiContributionV1 (see macro below).
class UiContributionV1
{
public:
    virtual ~UiContributionV1() = default;

    /// Dock widget content for manifest ui.dock. Return nullptr when none.
    virtual QWidget *createDockWidget( QWidget *parent ) { Q_UNUSED( parent ); return nullptr; }

    /// Menu actions contributed under the plugin menu (ui.menu_actions).
    virtual QList<QAction *> createMenuActions( QWidget *parent )
    {
        Q_UNUSED( parent );
        return {};
    }

    /// Settings page for ui.settings_page. The host takes ownership and
    /// destroys the widget with the Preferences dialog.
    virtual QWidget *createSettingsPage( QWidget *parent )
    {
        Q_UNUSED( parent );
        return nullptr;
    }

    /// Called when the host releases plugin UI (unload / workspace reset).
    /// Delete contributed widgets/actions here if the plugin retained them.
    virtual void releaseUi() {}
};

/// Second entry point symbol a UI plugin may export (declared Qt-free in
/// exprs/plugin_interface.h; re-declared here for readability).
// constexpr: see exprs/plugin_interface.h

/// Convenience macro for the UI contribution translation unit:
///   EXPRS_EXPORT_UI_CONTRIBUTION(org_example_ui::DemoUi)
#define EXPRS_EXPORT_UI_CONTRIBUTION(UiClass)                                  \
    extern "C" ::exprs::UiContributionV1 *EXPRS_createUiContributionV1()       \
    {                                                                          \
        try                                                                    \
        {                                                                      \
            return new UiClass();                                              \
        }                                                                      \
        catch ( ... )                                                          \
        {                                                                      \
            return nullptr;                                                    \
        }                                                                      \
    }

} // namespace exprs
