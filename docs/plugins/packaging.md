# Plugin Packages (local distribution)

A package is a plugin directory: `plugin.json` + payload (native library,
python package, `resources/`, licenses). v1 packages are plain directories;
an archive format would be a manifest-versioned extension, not a format
break.

## Lifecycle

```bash
sicnu_geo_rs_cli plugin install ./my-plugin        # validates, copies into user root
sicnu_geo_rs_cli plugin list                       # state: validated | disabled | ...
sicnu_geo_rs_cli plugin disable org.example.demo   # persisted in plugins.index.json
sicnu_geo_rs_cli plugin enable org.example.demo
sicnu_geo_rs_cli plugin uninstall org.example.demo
```

Install root: `~/.local/share/sicnu_geo_rs/plugins/<plugin-id>`.

## Install rules

1. The manifest is validated (full validator) **before** anything is copied.
2. Id takeover protection: the target directory is refused when it hosts a
   manifest declaring a **different** id.
3. Path-escape protection: only regular files and real subdirectories are
   copied; symlinks/devices are refused; every target path is verified to
   stay inside source and destination (zip-slip style checks).
4. Re-installing the same id replaces the previous payload (read-modify of
   other plugins is never required).

## Enable/disable

`plugins.index.json` next to the user plugin root records disabled ids.
Disabled is a registry state (`E5003` in diagnostics), distinct from
broken (`E1xxx`) or incompatible (`E2xxx`) — the Plugin Manager and
`plugin list` show all three.
