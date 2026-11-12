# Release archives

The `release-package` preset builds an optimized release and archives its
install tree with CPack:

```sh
cmake --preset release-package
cmake --build --preset release-package
```

The archive appears in `build-release/`, named
`sicnu-geo-rs-<version>-<platform>.tar.gz` on Unix or `.zip` on Windows.
The version comes from the root CMake configuration. To choose another
CPack generator, pass `-DCPACK_GENERATOR=ZIP` when configuring.

For an existing configured tree, enable `SICNU_ENABLE_PACKAGING` and build
its `package` target. Other presets leave packaging disabled by default.

Archives contain the files covered by CMake install rules. Deployment still
requires the corresponding Qt, QGIS, GDAL and other runtime dependencies.
See [OFFLINE_BUNDLE.md](OFFLINE_BUNDLE.md) for classroom bundles and
`build-appimage.sh` for the Linux AppImage workflow.
