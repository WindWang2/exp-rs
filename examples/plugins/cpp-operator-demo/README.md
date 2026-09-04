# Demo C++ Operator Plugin (out-of-tree SDK consumer)

Minimal third-party operator plugin for exp-rs. It contributes one operator,
`demo:stats`, which aggregates a numeric parameter array into
count/sum/mean/min/max. The point is not the math — it is the contract:

- builds with `find_package(ExpRS)` against an installed SDK only
- no exp-rs internal/private headers
- registers once, appears everywhere (Processing catalog, CLI, MCP tools)

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/exp-rs-install
cmake --build build
```

## Install & run

```bash
# copy the built plugin directory into the user plugin root
install_dir=~/.local/share/sicnu_geo_rs/plugins/org.example.cpp-operator-demo
mkdir -p "$install_dir"
cp plugin.json "$install_dir/"
cp build/libcpp_operator_demo.so "$install_dir/"

sicnu_geo_rs_cli plugin list
sicnu_geo_rs_cli run 'demo:stats' --param 'values=[1,2,3]' --json
```

## Files

| file | purpose |
|---|---|
| `plugin.json` | Manifest v1 — id, API/ABI versions, declared `demo:stats` contribution |
| `plugin.cpp` | Plugin lifecycle (`PluginV1`) + operator registration |
| `demo_operator.h` | The `RSOperator` implementation (SDK operator contract) |
