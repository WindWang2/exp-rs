# Pi package fixture

This fixture includes the production `cmake/PiRuntime.cmake` and CPack rules
without building Qt/QGIS. Pi packaging is opt-in and supports native Windows
x64. It never discovers a system Node installation or prepares dependencies
during CMake configure/install.

Prepare the immutable SDK explicitly with `scripts/prepare_pi_runtime.py`.
Then prepare the small wrapper separately:

```powershell
python -I -B scripts/prepare_pi_product.py --destination ABSOLUTE_PRODUCT_STAGE --sdk-stage ABSOLUTE_SDK_STAGE
cmake -S tools/ci/pi_runtime -B ABSOLUTE_BUILD -DPython_EXECUTABLE=ABSOLUTE_BUILD_PYTHON -DSICNU_PI_RUNTIME_STAGE=ABSOLUTE_SDK_STAGE -DSICNU_PI_PRODUCT_STAGE=ABSOLUTE_PRODUCT_STAGE
cmake --install ABSOLUTE_BUILD --prefix ABSOLUTE_PREFIX --component PiRuntime
cmake --install ABSOLUTE_BUILD --prefix ABSOLUTE_PREFIX --component PiProduct
python -I -B scripts/prepare_pi_product.py --verify-install ABSOLUTE_PREFIX
cpack --config ABSOLUTE_BUILD/CPackConfig.cmake -B ABSOLUTE_ARCHIVE_DIRECTORY
```

`PiRuntime` installs the unchanged P0 Node/SDK/auxiliary roots and raw SDK
receipt. `PiProduct` installs only `main.mjs`, `protocol.mjs`, and
`controlled-session.mjs` under `libexec/sicnu-geo-rs/pi-product`, plus
`share/sicnu-geo-rs/pi-product.json`. Install `PiRuntime` first. The final
`PiProduct` install rule validates the complete installed pair, as does full
install/CPack. Source stages are checked again before copying.
Full SDK configure/install validation has a 15-minute budget per check;
the small wrapper-stage check has 30 seconds. These are packaging budgets,
separate from the private protocol's handshake/model timeouts.

Wrapper preparation and `--verify-stage PREFIX --sdk-stage SDK` read the small
SDK receipt and wrapper files. They do not scan SDK payloads, invoke Node/npm,
download, or rewrite the SDK stage. `--verify-install PREFIX` additionally
validates every SDK asset. Stale, missing, modified, extra, incompatible, or
linked files cause failure. Build-time Python verification compares the
checkout recipes; the installed native verifier and Node entry use the
packaged receipts and their compiled/pinned identity.

The product receipt schema is `sicnu.pi-product/1`:

```json
{
  "schema": "sicnu.pi-product/1",
  "protocol_version": 1,
  "entry": "libexec/sicnu-geo-rs/pi-product/main.mjs",
  "requires_sdk": {
    "target": "x86_64-pc-windows-msvc",
    "pi_version": "1.5.2",
    "node_version": "24.22.3",
    "recipe_digest": "<64 lowercase hex characters>",
    "receipt_sha256": "<64 lowercase hex characters>"
  },
  "sources": [{"path": "pi/product-runtime/<allowlisted name>", "sha256": "<hex>"}],
  "files": [{"path": "libexec/sicnu-geo-rs/pi-product/<allowlisted name>", "sha256": "<hex>", "size": 123}]
}
```

The two arrays contain exactly the three allowlisted files in alphabetical
order. `recipe_digest` is SHA-256 of the SDK receipt's `recipes` object encoded
as UTF-8, sorted keys, compact JSON separators `,` and `:`, unescaped Unicode,
without BOM or trailing newline. `receipt_sha256` hashes the SDK receipt's
original bytes. `--print-recipe-digest` prints the current pinned digest without
checking the host platform or accessing any stage. Receipts contain no
absolute paths, timestamps, credentials, or user configuration.

The Windows workflow exercises real install/CPack, later source-stage
corruption, explicit offline SDK preparation, relocation into a path with
spaces and Chinese text, unavailable source stages, and the private protocol
with the relocated Node's outbound network blocked. Protocol startup is
separate from a model session and from Qt/NDVI delivery validation.
