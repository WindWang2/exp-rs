#!/usr/bin/env python3
"""exprs_new_plugin — scaffold a new ExpRS plugin (developer tooling).

Generates a minimal, conformance-shaped plugin directory for one of the
supported contribution kinds:

    cpp_operator     native library with an RSOperator (CMake + tests seam)
    python_operator  metadata.txt + SDK-decorated operator (out-of-process)
    external_tool    pure-manifest external executable (no code payload)
    model_runtime    native plugin registering a model runtime backend
    data_provider    native plugin contributing a data provider
    agent_tool       manifest agent tool (schema-only, executor optional)
    ui_contribution  native plugin contributing dock/menu/settings UI

Usage:
    exprs_new_plugin.py --kind external_tool --id org.acme.ndvi-tool \\
        --name "NDVI Tool" --out /path/to/plugins

The generated manifest pins api_version/abi_version to the SDK contract the
templates are maintained against (EXP_RS_PLUGIN_API_VERSION "3.0", ABI 1), so
the output passes `sicnu_geo_rs_cli plugin test` for manifest-kind plugins and
builds against ExpRS::SDK for native kinds. Keep the constants in sync with
src/sdk/exprs/version.h when the SDK moves.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

API_VERSION = "3.0"
ABI_VERSION = 1

TEMPLATES: dict[str, dict[str, object]] = {
    "external_tool": {
        "entrypoint_kind": "manifest",
        "capabilities": ["operator", "external_tools"],
        "permissions": ["external_process", "filesystem_read"],
        "files": {},
    },
    "agent_tool": {
        "entrypoint_kind": "manifest",
        "capabilities": ["agent_tool"],
        "permissions": [],
        "files": {},
    },
    "python_operator": {
        "entrypoint_kind": "python",
        "capabilities": ["python_processing", "operator"],
        "permissions": [],
        "files": {
            "metadata.txt": "[general]\nname={name}\nversion=0.1.0\n",
            "python/{package}/__init__.py": '''"""{name} — ExpRS python operator plugin."""

from exprs.plugin import operator, publish_to_iface


@operator(
    id="{operator_id}",
    display_name="{name}",
    group="python",
    description="Replace with a real operator implementation.",
    inputs=[{{"name": "text", "type": "string", "required": True}}],
)
def run(ctx, params):
    ctx.report_progress(1.0, "done")
    return {{"echo": params.get("text", "")}}


def register(iface) -> int:
    """Called by the plugin author from initGui(); publishes all operators."""
    return publish_to_iface(iface)
''',
        },
    },
    "cpp_operator": {
        "entrypoint_kind": "native",
        "capabilities": ["operator"],
        "permissions": [],
        "files": {
            "CMakeLists.txt": '''cmake_minimum_required(VERSION 3.24)
project({target} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ExpRS REQUIRED CONFIG)

add_library({target} SHARED plugin.cpp)
target_link_libraries({target} PRIVATE ExpRS::SDK)
set_target_properties({target} PROPERTIES PREFIX "lib" OUTPUT_NAME "{target}")
''',
            "plugin.cpp": '''// {name} — generated from the ExpRS cpp_operator template.
#include "exprs/plugin_interface.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <json/json.h>

#include <numeric>

namespace {{

class {class_name}Operator final : public sicnu::operators::RSOperator
{{
  public:
    std::string name() const override {{ return "{operator_id}"; }}
    std::string displayName() const override {{ return "{name}"; }}
    std::string group() const override {{ return "{group}"; }}
    std::string description() const override {{ return "{description}"; }}

    Json::Value schema() const override
    {{
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }}

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext & ) override
    {{
        // TODO: real implementation.
        if ( !params.isMember( "values" ) || !params["values"].isArray() )
            throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::InvalidParameter, "'values' array is required" );
        double sum = 0;
        for ( const Json::Value &value : params["values"] )
            sum += value.asDouble();
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["sum"] = sum;
        return result;
    }}
}};

class {class_name}Plugin final : public exprs::PluginV1
{{
  public:
    std::string pluginId() const override {{ return "{id}"; }}
    std::string version() const override {{ return "0.1.0"; }}

    bool initialize( exprs::HostServicesV1 & ) override {{ return true; }}
    void shutdown() override {{}}

    bool registerContributions( exprs::ContributionContextV1 &context ) override
    {{
        return context.registerOperatorFactory(
            "{operator_id}", []() -> std::unique_ptr<sicnu::operators::RSOperator> {{
                return std::make_unique<{class_name}Operator>();
            }} );
    }}
}};

}} // namespace

exprs::PluginV1 *EXPRS_createPluginV1()
{{
    try
    {{
        return new {class_name}Plugin();
    }}
    catch ( ... )
    {{
        return nullptr;
    }}
}}
''',
        },
    },
}

# Kinds that share the native C++ plugin skeleton with a different manifest.
for _kind, _capability in (
    ("model_runtime", "model_runtime"),
    ("data_provider", "data_provider"),
    ("ui_contribution", "ui"),
):
    TEMPLATES[_kind] = {
        "entrypoint_kind": "native",
        "capabilities": [_capability],
        "permissions": [],
        "files": dict(TEMPLATES["cpp_operator"]["files"]),
    }


def identifier_parts(plugin_id: str) -> dict[str, str]:
    if not re.fullmatch(r"[a-z0-9-]+(\.[a-z0-9-]+)+", plugin_id):
        raise SystemExit(f"plugin id '{plugin_id}' must be reverse-DNS lowercase (a.b.c)")
    parts = plugin_id.split(".")
    vendor = parts[0]
    name = parts[-1]
    words = [w for w in re.split(r"[^A-Za-z0-9]+", plugin_id) if w]
    return {
        "id": plugin_id,
        "vendor": vendor,
        "name": name,
        "operator_id": f"{vendor}:{name}",
        "class_name": "".join(w.capitalize() for w in words),
        "target": plugin_id.replace(".", "_").replace("-", "_"),
        "package": name.replace("-", "_"),
        "group": vendor,
    }


def build_manifest(kind: str, parts: dict[str, str], description: str) -> dict:
    template = TEMPLATES[kind]
    entrypoint = f"lib{parts['target']}.so" if template["entrypoint_kind"] == "native" else None
    manifest = {
        "manifest_version": 1,
        "id": parts["id"],
        "name": parts["name"],
        "version": "0.1.0",
        "api_version": API_VERSION,
        "abi_version": ABI_VERSION,
        "description": description,
        "capabilities": template["capabilities"],
        "permissions": template["permissions"],
    }
    if entrypoint:
        manifest["entrypoint"] = entrypoint
        manifest["entrypoint_kind"] = "native"
    elif kind == "python_operator":
        manifest["entrypoint_kind"] = "python"
        manifest["python"] = {"module": parts["package"], "package": f"python/{parts['package']}"}
    else:
        manifest["entrypoint_kind"] = "manifest"
    if kind == "external_tool":
        manifest["operators"] = [
            {
                "id": parts["operator_id"],
                "display_name": parts["name"],
                "group": parts["group"],
                "description": description,
                "inputs": [{"name": "text", "type": "string", "required": True}],
                "outputs": [],
                "external": {"argv": ["/bin/echo", "-n", "TODO:${text}"], "timeout_seconds": 300},
            }
        ]
    elif kind == "agent_tool":
        manifest["agent_tools"] = [
            {
                "id": parts["operator_id"],
                "display_name": parts["name"],
                "description": description,
                "input_schema": {"type": "object", "properties": {}},
            }
        ]
    elif kind == "ui_contribution":
        manifest["ui"] = {"dock": True, "menu_actions": True}
    elif kind in ("model_runtime", "data_provider"):
        key = "model_runtimes" if kind == "model_runtime" else "data_providers"
        entry = (
            {"framework": parts["operator_id"].split(":")[0], "display_name": parts["name"]}
            if kind == "model_runtime"
            else {
                "id": parts["operator_id"],
                "display_name": parts["name"],
                "schemes": [f"{parts['name']}://"],
            }
        )
        manifest[key] = [entry]
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", required=True, choices=sorted(TEMPLATES))
    parser.add_argument("--id", required=True, help="reverse-DNS plugin id, e.g. org.acme.tool")
    parser.add_argument("--name", help="display name (defaults to the id tail)")
    parser.add_argument("--description", default="Generated ExpRS plugin.")
    parser.add_argument("--out", required=True, help="target PARENT directory")
    args = parser.parse_args()

    parts = identifier_parts(args.id)
    parts["name"] = args.name or parts["name"]
    parts["description"] = args.description
    target_dir = Path(args.out) / args.id
    if target_dir.exists():
        raise SystemExit(f"refusing to overwrite existing {target_dir}")

    manifest = build_manifest(args.kind, parts, args.description)
    target_dir.mkdir(parents=True)
    (target_dir / "plugin.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    for relative, template in TEMPLATES[args.kind]["files"].items():  # type: ignore[index]
        text = template.format(**parts)
        path = target_dir / relative.format(**parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print(target_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
