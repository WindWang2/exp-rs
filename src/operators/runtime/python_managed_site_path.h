#pragma once

namespace sicnu::operators::python
{

// Native wheels derive their DLL directories from the imported module path.
// Use Windows' extended-path namespace for the existing managed site entry.
// Replacing it avoids duplicate distribution records from a second alias.
inline constexpr char kManagedSitePathBootstrap[] = R"PY(
def _sicnu_enable_managed_site_path():
    import os, sys
    if sys.platform != 'win32':
        return
    separator = chr(92)
    extended_prefix = separator * 2 + '?' + separator

    def plain_path(path):
        path = os.path.normpath(os.path.abspath(path))
        if path.startswith(extended_prefix):
            path = path[len(extended_prefix):]
            if path[:4].upper() == 'UNC' + separator:
                path = separator * 2 + path[4:]
        return path

    managed_site = plain_path(os.path.join(sys.prefix, 'Lib', 'site-packages'))
    managed_key = os.path.normcase(managed_site)
    if managed_site.startswith(separator * 2):
        extended_site = extended_prefix + 'UNC' + separator + managed_site[2:]
    else:
        extended_site = extended_prefix + managed_site
    for index, entry in enumerate(sys.path):
        if isinstance(entry, str) and os.path.normcase(plain_path(entry)) == managed_key:
            sys.path[index] = extended_site

_sicnu_enable_managed_site_path()
del _sicnu_enable_managed_site_path
)PY";

} // namespace sicnu::operators::python
