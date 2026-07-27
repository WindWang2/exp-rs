def classFactory(iface):
    return SamplePlugin(iface)

class SamplePlugin:
    def __init__(self, iface):
        self.iface = iface
        self.initialized = False

    def initGui(self):
        self.initialized = True

    def unload(self):
        self.initialized = False
