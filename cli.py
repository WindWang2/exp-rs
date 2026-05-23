import sys
import argparse
from engine.registry import ToolRegistry
import engine._processing # Imports processing so all standard tools register themselves on import

def main():
    parser = argparse.ArgumentParser(description="Antigravity RS — Headless CLI Companion")
    subparsers = parser.add_subparsers(dest="command", help="Command to run")
    
    # 1. 'list' subcommand
    list_parser = subparsers.add_parser("list", help="Lists all registered geospatial tools")
    list_parser.add_argument("--json", action="store_true", help="Output list in raw JSON format")
    
    # 2. 'run' subcommand
    run_parser = subparsers.add_parser("run", help="Executes a registered processing tool headless")
    run_parser.add_argument("tool", type=str, help="Name of the registered tool to run")
    
    # Parse initial known arguments to handle dynamic run parameters
    args, unknown = parser.parse_known_args()
    
    registry = ToolRegistry()
    
    if args.command == "list":
        tools = registry.list_tools()
        if args.json:
            import json
            # Strip functions for JSON serializability
            serializable = []
            for t in tools:
                tc = t.copy()
                del tc["fn"]
                serializable.append(tc)
            print(json.dumps(serializable, indent=2))
        else:
            print("ANTIGRAVITY RS — REGISTERED GEOSPATIAL TOOLS")
            print("===========================================")
            for t in tools:
                print(f"* {t['name']} ({t['category']})")
                print(f"  Label: {t['label']}")
                print(f"  Description: {t['description']}")
                print("  Parameters:")
                for p in t["params"]:
                    req = "required" if p.get("required", True) else "optional"
                    default_str = f" (default: {p['default']})" if "default" in p and p["default"] is not None else ""
                    print(f"    --{p['name']}: {p['type']} ({req}){default_str} - {p.get('help', '')}")
                print()
                
    elif args.command == "run":
        tool_name = args.tool
        tool = registry.get_tool(tool_name)
        if not tool:
            print(f"Error: Tool '{tool_name}' is not registered.")
            sys.exit(1)
            
        # Dynamically build argument parser for the specific tool parameters
        tool_parser = argparse.ArgumentParser(description=tool["description"])
        for p in tool["params"]:
            ptype = p["type"]
            arg_type = str
            if ptype == "int":
                arg_type = int
            elif ptype == "float":
                arg_type = float
            elif ptype == "bool":
                arg_type = lambda x: x.lower() == 'true'
                
            tool_parser.add_argument(
                f"--{p['name']}",
                type=arg_type,
                required=p.get("required", True),
                default=p.get("default"),
                help=p.get("help", "")
            )
            
        # Parse unknown dynamic arguments
        tool_args = tool_parser.parse_args(unknown)
        params = vars(tool_args)
        
        print(f"Running '{tool['label']}' headless...")
        try:
            output_file = tool["fn"](**params)
            print(f"Success! Output written to: {output_file}")
        except Exception as e:
            print(f"Error executing tool: {e}", file=sys.stderr)
            sys.exit(1)
            
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
