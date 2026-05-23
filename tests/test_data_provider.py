import pytest
from engine.core.display.base.data_provider import DataProvider

def test_data_provider_abstract():
    with pytest.raises(TypeError):
        DataProvider()

def test_data_provider_concrete():
    class ConcreteProvider(DataProvider):
        def extent(self):
            return [0, 0, 10, 10]
    
    provider = ConcreteProvider()
    assert provider.extent() == [0, 0, 10, 10]
