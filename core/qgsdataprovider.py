from abc import ABC, abstractmethod

class QgsDataProvider(ABC):
    @abstractmethod
    def extent(self):
        pass


DataProvider = QgsDataProvider
