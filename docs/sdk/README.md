# ExpRS Public SDK — install & Hello Operator

The SDK is a plain CMake package produced by installing an exp-rs build.

## Install

```bash
cmake --install <exp-rs-build-dir> --prefix ~/exp-rs-sdk
```

Layout:

```
~/exp-rs-sdk
├── include/exprs/                  # public SDK headers (namespace exprs)
├── include/operators/framework/    # the RSOperator contract headers
├── lib/libsicnu_operators_core.a   # operator link leaf (context/error impl)
├── lib/cmake/ExpRS/                # find_package(ExpRS) package
└── share/exp-rs/schemas/           # versioned JSON schemas
```

## Consume

```cmake
find_package(ExpRS 3.0 CONFIG REQUIRED)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE ExpRS::SDK)

# Exported for manifest generation:
#   ${ExpRS_PLUGIN_API_VERSION}  e.g. "3.0"
#   ${ExpRS_PLUGIN_ABI_VERSION}  e.g. 1
```

`ExpRS::SDK` carries the include paths, `jsoncpp` (the operator JSON type),
and the `sicnu_operators_core` static leaf. It intentionally does NOT carry
the whole `sicnu_operators`/`sicnu_processing` libraries, Qt, or QGIS —
plugins compiled against only these headers are the stable third-party tier.

## Hello Operator

`my_plugin.cpp`:

```cpp
#include "exprs/plugin_interface.h"
#include "operators/framework/rs_operator.h"

using namespace sicnu::operators;

class HelloOperator : public RSOperator {
public:
  std::string name() const override { return "me:hello"; }
  std::string displayName() const override { return "Hello"; }
  Json::Value schema() const override {
    Json::Value s(Json::objectValue);
    s["type"] = "object";
    return s;
  }
  Json::Value run(const Json::Value& params, RSOperatorContext& ctx) override {
    ctx.reportProgress(1.0, "hello");
    Json::Value out(Json::objectValue);
    out["success"] = true;
    out["message"] = "hello from a plugin";
    return out;
  }
};

class MyPlugin : public exprs::PluginV1 {
public:
  std::string pluginId() const override { return "me.hello"; }
  bool initialize(exprs::HostServicesV1&) override { return true; }
  void registerContributions(exprs::ContributionContextV1& ctx) override {
    ctx.registerOperatorFactory("me:hello", []() -> std::unique_ptr<RSOperator> {
      return std::make_unique<HelloOperator>();
    });
  }
  void shutdown() override {}
};

EXPRS_EXPORT_PLUGIN(MyPlugin)
```

`plugin.json`:

```json
{
  "manifest_version": 1,
  "id": "me.hello",
  "name": "Hello Plugin",
  "version": "1.0.0",
  "api_version": "3.0",
  "abi_version": 1,
  "entrypoint": "libmy_plugin.so",
  "entrypoint_kind": "native",
  "capabilities": ["operator"],
  "operators": [
    { "id": "me:hello", "display_name": "Hello", "inputs": [], "outputs": [] }
  ]
}
```

Build, install into `~/.local/share/sicnu_geo_rs/plugins/me.hello/`, and the
operator appears in `sicnu_geo_rs_cli algorithms list` and `run me:hello`.

## What the SDK deliberately does NOT expose

- Qt / QWidget types (except the build-locked `exprs/plugin_ui.h`)
- JobEngine, TaskCenter, DataManager internals
- QGIS implementation headers
- any `src/` private header

See also: [versioning.md](versioning.md),
[../plugins/manifest-v1.md](../plugins/manifest-v1.md).
