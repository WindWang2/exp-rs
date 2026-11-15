# Windows application packaging

The Pi backend uses the packaged Node and SDK. `SICNU_BUNDLE_PI` defaults to OFF
during migration; enabling packaging does not select the conversation backend.
Preparation is explicit and application startup never runs npm or downloads a
runtime. A build-time Python interpreter is required; the installed application
uses its bundled Python 3.12.16 for managed scripts.

Prepare the immutable SDK with `scripts/prepare_pi_runtime.py --destination
<sdk-prefix> --cache-dir <cache> --node-archive <verified-official-zip>`.
Use `--offline` after populating the cache. The Node lock, production package
lock and AuthContext patch are verified before publishing the stage.

Prepare the small first-party wrapper separately with
`scripts/prepare_pi_product.py --destination <product-prefix> --sdk-stage
<sdk-prefix>`. A wrapper edit needs a new product stage, while the SDK remains
unchanged. The product receipt binds the complete SDK receipt and its vendor
recipe. Install both `PiRuntime` and `PiProduct` components together; a wrapper
component without its verified SDK fails.

Configure a native Windows x64 Release build with:

```text
-DSICNU_EMBED_PYTHON=OFF
-DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON
-DSICNU_BUNDLE_PYTHON=ON -DSICNU_PYTHON_RUNTIME_STAGE=<python-prefix>
-DSICNU_BUNDLE_PI=ON -DSICNU_PI_RUNTIME_STAGE=<sdk-prefix>
-DSICNU_PI_PRODUCT_STAGE=<product-prefix>
```

Configure and install verify the selected stages. SDK verification may take
several minutes on a machine scanning many small files; it has a separate
15-minute budget from the model handshake. No stage is prepared implicitly.

After building the application, translations, CLI and worker, run
`scripts/stage_windows_application.ps1 -BuildDir <release-build> -RuntimeStage
<python-prefix> -PackageRoot <fresh-repository-dist-child> -Cmake <cmake.exe>`.
Qt, QCA and QtKeychain default to the paths recorded in the CMake cache and may
be supplied explicitly. The script requires dumpbin beside the configured
compiler, app-local release CRT files and a complete configured dependency set.
It refuses Debug CRT, host Python imports, unresolved non-system PE imports
and output reparse points. Only the actual Windows x64 Node prebuild is checked
as a PE; upstream resources for other platforms remain in the SDK receipt.

The script preserves the Python/SDK/product receipts, deploys both Chinese
translation catalogs, writes the complete application manifest and creates a
ZIP using bundled Python's standard library. Each receipted file, including
hidden and long-path resources, is hashed while archiving. It never overwrites
an existing release ZIP. The ZIP's SHA-256 is written beside it.

Extract the ZIP into a fresh path outside the repository, with spaces and
Chinese characters. Validate the extracted receipts and run the packaged
private protocol with a local model fixture and no system Node/npm/Python in
PATH. These packaging checks are separate from the Qt approval, Python NDVI,
real model and visible bilingual delivery acceptance in the implementation
ledger. StepFun inference still requires access to the configured model API.
