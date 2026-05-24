import enum
from core.qgis import Qgis


class QgsUnitTypes:
    """Unit types and conversion utilities for distance and area measurements."""

    class AreaUnit(enum.IntEnum):
        SquareMeters = 0
        SquareKilometers = 1
        SquareFeet = 2
        SquareYards = 3
        SquareMiles = 4
        Hectares = 5
        Acres = 6

    # Conversion factors from each unit to meters
    _DISTANCE_TO_METERS = {
        Qgis.DistanceUnit.Meters: 1.0,
        Qgis.DistanceUnit.Kilometers: 1000.0,
        Qgis.DistanceUnit.Feet: 0.3048,
        Qgis.DistanceUnit.Yards: 0.9144,
        Qgis.DistanceUnit.Miles: 1609.344,
        Qgis.DistanceUnit.NauticalMiles: 1852.0,
        Qgis.DistanceUnit.Degrees: 111319.490793,
    }

    @staticmethod
    def fromUnitToUnitFactor(
        from_unit: Qgis.DistanceUnit, to_unit: Qgis.DistanceUnit
    ) -> float:
        """Return the conversion factor from *from_unit* to *to_unit*."""
        if from_unit == to_unit:
            return 1.0
        from_factor = QgsUnitTypes._DISTANCE_TO_METERS.get(from_unit, 1.0)
        to_factor = QgsUnitTypes._DISTANCE_TO_METERS.get(to_unit, 1.0)
        return from_factor / to_factor

    @staticmethod
    def toAbbreviatedString(unit: Qgis.DistanceUnit) -> str:
        """Return a short string abbreviation for *unit*."""
        abbreviations = {
            Qgis.DistanceUnit.Meters: "m",
            Qgis.DistanceUnit.Kilometers: "km",
            Qgis.DistanceUnit.Feet: "ft",
            Qgis.DistanceUnit.Degrees: "deg",
        }
        return abbreviations.get(unit, "")
