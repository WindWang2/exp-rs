from abc import ABC, abstractmethod

class DataProvider(ABC):
    @abstractmethod
    def extent(self):
        pass
