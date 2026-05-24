from abc import ABC, abstractmethod


class QgsFeatureSink(ABC):
    @abstractmethod
    def addFeature(self, feature) -> bool:
        pass

    def addFeatures(self, features: list) -> bool:
        success = True
        for f in features:
            if not self.addFeature(f):
                success = False
        return success

    def finalize(self) -> bool:
        return True
