import enum


class Qgis:
    """Namespace for QGIS-style enums used throughout the framework."""

    class GeometryType(enum.IntEnum):
        Point = 0
        Line = 1
        Polygon = 2
        Unknown = 100
        Null = 101

    class LayerType(enum.IntEnum):
        Raster = 0
        Vector = 1
        Plugin = 2
        Mesh = 3

    class DataType(enum.IntEnum):
        Byte = 1
        UInt16 = 2
        Int16 = 3
        UInt32 = 4
        Int32 = 5
        Float32 = 6
        Float64 = 7
        CInt16 = 8
        CInt32 = 9
        CFloat32 = 10
        CFloat64 = 11
        ARGB32 = 12
        ARGB32_Premultiplied = 13

    class DistanceUnit(enum.IntEnum):
        Meters = 0
        Kilometers = 10
        Degrees = 1
        Feet = 2
        NauticalMiles = 3
        Yards = 4
        Miles = 5
        DegreesMinutesSeconds = 6
        UnknownUnit = 7

    class RasterLayerType(enum.IntEnum):
        GrayOrUndefined = 0
        Multiband = 1
        Palette = 2
        SingleBandColorData = 3
