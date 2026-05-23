class MapSettings:
    def __init__(self):
        self.layers = []
        self.extent = None
        self.output_size = None # QSize
        self.destination_crs = "EPSG:3857"
