import os
import json
import urllib.request
from typing import Dict, Any, Tuple

from analysis.qgsprocessingregistry import ToolRegistry

class AgentExecutor:
    """
    Sandboxed, declarative JSON tool coordinator.
    Translates natural language to registered GIS tools without in-app python execution.
    """
    def __init__(self):
        self.registry = ToolRegistry()
        
    def generate_system_prompt(self) -> str:
        schemas = self.registry.get_agent_schemas()
        return (
            "You are Antigravity RS, an advanced Remote Sensing AI assistant designed for university labs.\n"
            "Your job is to translate natural language user requests into a structured tool execution plan.\n"
            "You must respond ONLY with a raw JSON object containing these keys:\n"
            "1. 'thought': Explain your reasoning and spectral analysis choices to the student.\n"
            "2. 'tool_call': A dictionary with 'name' and 'params' matching the registered tools schema.\n"
            "3. 'code': A clean, readable equivalent PyQGIS/Python script utilizing the registered tools for study.\n\n"
            "AVAILABLE REGISTERED TOOLS SCHEMAS:\n"
            f"{json.dumps(schemas, indent=2)}\n\n"
            "Example response structure:\n"
            "{\n"
            "  \"thought\": \"Calculating NDVI to assess vegetation vigor...\",\n"
            "  \"tool_call\": {\"name\": \"calculate_ndvi\", \"params\": {\"input_path\": \"data/sample_crops.tif\", \"output_path\": \"data/output_ndvi.tif\", \"red_band\": 1, \"nir_band\": 3}},\n"
            "  \"code\": \"# NDVI Calculation\\nfrom analysis.processing.qgsindices import calculate_ndvi\\ncalculate_ndvi('data.tif', 'ndvi.tif', 1, 3)\"\n"
            "}"
        )

    def execute_chat(self, user_prompt: str) -> Dict[str, Any]:
        """
        Executes chat via Gemini API. Falls back to highly intelligent
        local offline rule engine if no API key is present or request fails.
        """
        api_key = os.environ.get("GEMINI_API_KEY")
        if api_key:
            try:
                system_prompt = self.generate_system_prompt()
                url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key={api_key}"
                
                payload = {
                    "contents": [{
                        "parts": [{"text": f"SYSTEM: {system_prompt}\n\nUSER: {user_prompt}"}]
                    }],
                    "generationConfig": {
                        "responseMimeType": "application/json"
                    }
                }
                
                req = urllib.request.Request(
                    url,
                    data=json.dumps(payload).encode('utf-8'),
                    headers={'Content-Type': 'application/json'},
                    method='POST'
                )
                
                with urllib.request.urlopen(req, timeout=8) as response:
                    res_data = json.loads(response.read().decode('utf-8'))
                    text_out = res_data["candidates"][0]["content"]["parts"][0]["text"]
                    return json.loads(text_out.strip())
            except Exception as e:
                print(f"Gemini API request failed ({e}). Falling back to offline rule dispatcher.")
                
        # Smart rule-based offline dispatcher
        return self._execute_offline_fallback(user_prompt)

    def _execute_offline_fallback(self, prompt: str) -> Dict[str, Any]:
        """Highly intelligent local fallback engine for offline execution."""
        p_lower = prompt.lower()
        
        # Default fallback template
        res = {
            "thought": "I am operating in offline mode. I detected your intent and formulated a local execution plan.",
            "tool_call": None,
            "code": "# Operating offline"
        }
        
        # Map sample file paths relative to launch directory
        sample_path = "data/sample_crops.tif"
        
        if "dos1" in p_lower or "atmospheric" in p_lower:
            res["thought"] = "Detected DOS1 atmospheric correction request. Executing dark object subtraction..."
            res["tool_call"] = {
                "name": "dos1_correction",
                "params": {
                    "input_path": sample_path,
                    "output_path": "data/output_dos1.tif"
                }
            }
            res["code"] = (
                "# Educational Script: DOS1 Atmospheric Correction\n"
                "from analysis.preprocessing.qgsatmospherictreatment import calculate_dos1\n\n"
                f"input_raster = '{sample_path}'\n"
                "output_dos1 = 'data/output_dos1.tif'\n\n"
                "calculate_dos1(input_raster, output_dos1)"
            )
        elif "ndvi" in p_lower or "vegetation" in p_lower or "crops" in p_lower:
            res["thought"] = "Detected NDVI vegetation calculation request. Running Red/NIR band index division..."
            res["tool_call"] = {
                "name": "calculate_ndvi",
                "params": {
                    "input_path": sample_path,
                    "output_path": "data/output_ndvi.tif",
                    "red_band": 1,
                    "nir_band": 3
                }
            }
            res["code"] = (
                "# Educational Script: Normalized Difference Vegetation Index\n"
                "from analysis.processing.qgsindices import calculate_ndvi\n\n"
                f"input_raster = '{sample_path}'\n"
                "output_ndvi = 'data/output_ndvi.tif'\n\n"
                "# Execute band algebra in background thread\n"
                "calculate_ndvi(input_raster, output_ndvi, red_band=1, nir_band=3)"
            )
        elif "ndwi" in p_lower or "water" in p_lower or "river" in p_lower:
            res["thought"] = "Detected NDWI open-water detection request. Running Green/NIR band index division..."
            res["tool_call"] = {
                "name": "calculate_ndwi",
                "params": {
                    "input_path": sample_path,
                    "output_path": "data/output_ndwi.tif",
                    "green_band": 2,
                    "nir_band": 3
                }
            }
            res["code"] = (
                "# Educational Script: Normalized Difference Water Index\n"
                "from analysis.processing.qgsindices import calculate_ndwi\n\n"
                f"input_raster = '{sample_path}'\n"
                "output_ndwi = 'data/output_ndwi.tif'\n\n"
                "calculate_ndwi(input_raster, output_ndwi, green_band=2, nir_band=3)"
            )
        elif "kmeans" in p_lower or "classify" in p_lower or "unsupervised" in p_lower:
            res["thought"] = "Detected K-Means unsupervised land classification request. Splitting stacked spectral pixels into 5 classes..."
            res["tool_call"] = {
                "name": "kmeans_classify",
                "params": {
                    "input_path": sample_path,
                    "output_path": "data/output_classification.tif",
                    "bands": "1,2,3",
                    "clusters": 5
                }
            }
            res["code"] = (
                "# Educational Script: K-Means Unsupervised Classification\n"
                "from analysis.processing.qgsclassification import kmeans_classify\n\n"
                "kmeans_classify(\n"
                f"    input_path='{sample_path}',\n"
                "    output_path='data/output_classification.tif',\n"
                "    bands='1,2,3',\n"
                "    clusters=5\n"
                ")"
            )
        else:
            res["thought"] = "I received your message but could not match it to a standard GIS tool offline. Try asking: 'Calculate NDVI on sample_crops' or 'Run unsupervised K-Means classification'."
            
        return res

    def dispatch_tool(self, tool_call: Dict[str, Any]) -> str:
        """Safe tool executor: non-evaluating dispatcher running registered functions."""
        if not tool_call:
            raise ValueError("No tool call provided")
            
        name = tool_call["name"]
        params = tool_call["params"]
        
        tool = self.registry.get_tool(name)
        if not tool:
            raise ValueError(f"Tool '{name}' is not registered")
            
        # Execute the registered function directly
        return tool["fn"](**params)
