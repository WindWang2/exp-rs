import numpy as np
from core.raster.qgsrasterblock import QgsRasterBlock


def test_block_creation():
    block = QgsRasterBlock(width=10, height=5, no_data_value=-9999.0)
    assert block.width() == 10
    assert block.height() == 5
    assert not block.isEmpty()


def test_block_default_values():
    block = QgsRasterBlock(width=3, height=3, no_data_value=-1.0)
    assert block.value(0, 0) == -1.0
    assert block.value(2, 2) == -1.0


def test_block_set_value():
    block = QgsRasterBlock(width=3, height=3)
    block.setValue(1, 1, 42.0)
    assert block.value(1, 1) == 42.0


def test_block_from_numpy():
    arr = np.array([[1, 2], [3, 4]], dtype=np.float32)
    block = QgsRasterBlock.from_numpy(arr, no_data_value=-9999.0)
    assert block.width() == 2
    assert block.height() == 2
    assert block.value(0, 0) == 1.0
    assert block.noDataValue() == -9999.0


def test_block_data():
    arr = np.array([[10, 20], [30, 40]], dtype=np.float32)
    block = QgsRasterBlock.from_numpy(arr)
    data = block.data()
    assert data[1, 1] == 40.0


def test_block_is_empty():
    block = QgsRasterBlock()
    assert block.isEmpty()


def test_block_set_matrix():
    block = QgsRasterBlock()
    arr = np.ones((5, 5), dtype=np.float32)
    block.setMatrix(arr)
    assert block.width() == 5
    assert block.height() == 5
    assert not block.isEmpty()
