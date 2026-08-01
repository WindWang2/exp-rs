def classFactory(iface):
    iface.registerProcessingAlgorithm(
        "py:echo_plugin",
        "Echo Plugin",
        execute_fn=lambda p: {"echo": p},
    )
    return EchoPlugin()


class EchoPlugin:
    def initGui(self):
        pass

    def unload(self):
        pass
