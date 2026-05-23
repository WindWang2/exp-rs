from agent.executor import AgentExecutor
import engine._preprocessing # Ensure the tool is registered

def test_agent_dos1():
    executor = AgentExecutor()
    res = executor.execute_chat("Run DOS1 atmospheric correction on sample_crops.tif")
    
    assert res["tool_call"] is not None
    assert res["tool_call"]["name"] == "dos1_correction"
    assert "data/sample_crops.tif" in res["tool_call"]["params"]["input_path"]
    
    print("Agent DOS1 E2E test passed!")

if __name__ == "__main__":
    test_agent_dos1()
