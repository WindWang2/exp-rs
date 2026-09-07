def classFactory(iface):
    # Issue #755 real-path fixture: registers a py: algorithm through the
    # plugin's own worker connection so the host bridge tracks and revokes
    # it across load/unload round-trips.
    iface.registerProcessingAlgorithm(
        "py:sample_echo",
        "Sample Echo",
        execute_fn=lambda p: {"echo": p},
    )
    return SamplePlugin(iface)


class SamplePlugin:
    def __init__(self, iface):
        self.iface = iface
        self.initialized = False

    def initGui(self):
        self.initialized = True

    def unload(self):
        self.initialized = False
