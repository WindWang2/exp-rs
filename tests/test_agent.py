import pytest
from agent.executor import AgentExecutor
from engine.registry import ToolRegistry

def test_agent_executor_prompt_generation():
    executor = AgentExecutor()
    prompt = executor.generate_system_prompt()
    
    # Assert crucial keywords are in the system prompt
    assert "Antigravity RS" in prompt
    assert "tool_call" in prompt
    assert "calculate_ndvi" in prompt
    assert "kmeans_classify" in prompt

def test_agent_executor_offline_fallbacks():
    executor = AgentExecutor()
    
    # Test NDVI search matching
    res_ndvi = executor.execute_chat("Hey, calculate NDVI for me please!")
    assert "NDVI" in res_ndvi["thought"] or "vegetation" in res_ndvi["thought"].lower()
    assert res_ndvi["tool_call"]["name"] == "calculate_ndvi"
    assert "red_band" in res_ndvi["tool_call"]["params"]
    assert "nir_band" in res_ndvi["tool_call"]["params"]
    assert "from engine import calculate_ndvi" in res_ndvi["code"]
    
    # Test KMeans classification matching
    res_kmeans = executor.execute_chat("Run an unsupervised Kmeans classify with 6 clusters")
    assert "K-Means" in res_kmeans["thought"] or "kmeans" in res_kmeans["thought"].lower()
    assert res_kmeans["tool_call"]["name"] == "kmeans_classify"
    assert res_kmeans["tool_call"]["params"]["clusters"] == 5 or "clusters" in res_kmeans["tool_call"]["params"]
    assert "kmeans_classify" in res_kmeans["code"]

def test_agent_schema_formatting():
    registry = ToolRegistry()
    schemas = registry.get_agent_schemas()
    
    # Verify we generate compliant OpenAPI-style schemas
    assert len(schemas) > 0
    ndvi_schema = next((s for s in schemas if s["name"] == "calculate_ndvi"), None)
    assert ndvi_schema is not None
    assert ndvi_schema["parameters"]["type"] == "object"
    assert "input_path" in ndvi_schema["parameters"]["properties"]
    assert "red_band" in ndvi_schema["parameters"]["properties"]
    assert "input_path" in ndvi_schema["parameters"]["required"]
