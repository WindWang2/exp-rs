from typing import Callable, Dict, Any, List

class ToolRegistry:
    """
    Centralized Declarative Processing Toolbox Registry.
    Unifies dynamic UI parameter auto-generation and AI Agent JSON schemas.
    """
    _instance = None

    def __new__(cls, *args, **kwargs):
        if not cls._instance:
            cls._instance = super(ToolRegistry, cls).__new__(cls, *args, **kwargs)
            cls._instance.tools = {}
        return cls._instance

    def register(self, name: str, label: str, category: str, description: str, params: List[Dict[str, Any]], fn: Callable):
        """Registers a tool with its schema and executable function."""
        self.tools[name] = {
            "name": name,
            "label": label,
            "category": category,
            "description": description,
            "params": params,
            "fn": fn
        }

    def get_tool(self, name: str) -> Dict[str, Any]:
        return self.tools.get(name)

    def list_tools(self) -> List[Dict[str, Any]]:
        return list(self.tools.values())
        
    def get_agent_schemas(self) -> List[Dict[str, Any]]:
        """Generates schema definitions for LLM tool calling."""
        schemas = []
        for name, tool in self.tools.items():
            properties = {}
            required = []
            for p in tool["params"]:
                ptype = p["type"]
                param_schema = {}
                
                if ptype == "file":
                    param_schema["type"] = "string"
                    param_schema["description"] = p.get("help", f"Path to {p['label']}")
                elif ptype == "select":
                    param_schema["type"] = "string"
                    param_schema["description"] = p.get("help", p["label"])
                    if "options" in p:
                        param_schema["enum"] = p["options"]
                elif ptype == "float":
                    param_schema["type"] = "number"
                    param_schema["description"] = p.get("help", p["label"])
                elif ptype == "int":
                    param_schema["type"] = "integer"
                    param_schema["description"] = p.get("help", p["label"])
                elif ptype == "bool":
                    param_schema["type"] = "boolean"
                    param_schema["description"] = p.get("help", p["label"])
                else:
                    param_schema["type"] = "string"
                    param_schema["description"] = p.get("help", p["label"])
                
                properties[p["name"]] = param_schema
                if p.get("required", True):
                    required.append(p["name"])
            
            schemas.append({
                "name": name,
                "description": tool["description"],
                "parameters": {
                    "type": "object",
                    "properties": properties,
                    "required": required
                }
            })
        return schemas

import inspect

def register_tool(name: str = None, label: str = None, category: str = "Custom Tools", description: str = "", params: list = None):
    """
    Decorator to dynamically register a geospatial calculation function in ToolRegistry.
    Automatically parses signature parameter types and default values.
    """
    def decorator(fn):
        tool_name = name or fn.__name__
        tool_label = label or tool_name.replace("_", " ").title()
        tool_desc = description or (fn.__doc__.strip() if fn.__doc__ else "")
        
        # If parameters schema is omitted, dynamically inspect function signature
        tool_params = params
        if tool_params is None:
            tool_params = []
            sig = inspect.signature(fn)
            for param_name, param in sig.parameters.items():
                annotation = param.annotation
                ptype = "string"
                if annotation == int:
                    ptype = "int"
                elif annotation == float:
                    ptype = "float"
                elif annotation == bool:
                    ptype = "bool"
                elif "path" in param_name.lower() or "file" in param_name.lower():
                    ptype = "file"
                
                default = param.default if param.default != inspect.Parameter.empty else None
                required = param.default == inspect.Parameter.empty
                
                tool_params.append({
                    "name": param_name,
                    "label": param_name.replace("_", " ").title(),
                    "type": ptype,
                    "default": default,
                    "required": required,
                    "help": f"Enter value for {param_name}"
                })
                
        ToolRegistry().register(tool_name, tool_label, category, tool_desc, tool_params, fn)
        return fn
    return decorator

