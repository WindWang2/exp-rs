#pragma once

// support/qsettings_redirect.h — per-process QSettings isolation for
// Qt-linked test binaries (#1392 WP-J). Lives in its own header (not
// exprs_test_env.h) so the no-Qt sdk/io consumers of that header stay
// Qt-free; it reuses that header's localPid/scratchRoot discipline.
//
// Redirects the IniFormat UserScope QSettings root to a pid-unique scratch.
// Without it, `QSettings()` in test code lands in the REAL
// ~/.config/<org>/<app>.conf — parallel one-case-per-process runs of the same
// binary share that file and each case's clear()/write tramples the other's
// mid-assertion. Install ONE object at namespace scope per test binary;
// it constructs at static-init (before any QSettings use) and removes the
// scratch at process end, so no per-case conf files leak into the real
// profile.

#include <QSettings>
#include <QString>

#include <filesystem>
#include <string>

#include "exprs_test_env.h" // localPid / scratchRoot discipline

namespace exprs_test
{

struct QSettingsUserRootRedirect
{
    const std::string scratch;
    QSettingsUserRootRedirect()
        : scratch( scratchRoot( "qsettings_userroot" ) )
    {
        std::error_code ec;
        std::filesystem::create_directories( scratch, ec );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope,
                            QString::fromStdString( scratch ) );
    }
    ~QSettingsUserRootRedirect()
    {
        std::error_code ec;
        std::filesystem::remove_all( scratch, ec );
    }
    QSettingsUserRootRedirect( const QSettingsUserRootRedirect & ) = delete;
    QSettingsUserRootRedirect &operator=( const QSettingsUserRootRedirect & ) = delete;
};

} // namespace exprs_test
