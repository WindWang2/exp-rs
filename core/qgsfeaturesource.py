from abc import ABC, abstractmethod


class QgsFeatureSource(ABC):
    @abstractmethod
    def getFeatures(self, request=None):
        pass

    @abstractmethod
    def sourceName(self) -> str:
        pass

    def sourceCrs(self):
        return None

    @abstractmethod
    def fields(self):
        pass

    @abstractmethod
    def wkbType(self) -> int:
        return 0

    @abstractmethod
    def featureCount(self) -> int:
        return 0

    @abstractmethod
    def sourceExtent(self):
        pass
