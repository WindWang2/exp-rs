import pytest
from PySide6.QtCore import QSize
from PySide6.QtGui import QImage
from engine.core.display.pipeline.renderer_job import MapRendererJob

class MockSettings:
    def __init__(self, output_size):
        self.output_size = output_size
        self.layers = []

def test_map_renderer_job_emits_finished(qtbot):
    settings = MockSettings(QSize(100, 100))
    job = MapRendererJob(settings)
    
    with qtbot.waitSignal(job.signals.finished, timeout=1000) as blocker:
        job.run()
    
    assert isinstance(blocker.args[0], QImage)
    assert blocker.args[0].size() == QSize(100, 100)

def test_map_renderer_job_cancel(qtbot):
    settings = MockSettings(QSize(100, 100))
    job = MapRendererJob(settings)
    job.cancel()
    
    # finished should NOT be emitted if canceled before run
    from pytestqt.exceptions import TimeoutError
    with pytest.raises(TimeoutError):
        with qtbot.waitSignal(job.signals.finished, timeout=500):
            job.run()
