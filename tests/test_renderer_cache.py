"""Tests for QgsMapRendererCache — thread-safe LRU cache for per-layer rendered QImages."""

import threading
import time

import pytest
from PySide6.QtGui import QImage

from core.maprenderer.qgsmaprenderercache import QgsMapRendererCache


def _make_image(width: int = 4, height: int = 4, color: int = 0xFFFF0000) -> QImage:
    """Create a small ARGB32 image filled with *color*."""
    img = QImage(width, height, QImage.Format.Format_ARGB32)
    img.fill(color)
    return img


# --------------------------------------------------------------------------- #
#  Basic set / get
# --------------------------------------------------------------------------- #
class TestCacheSetAndGet:
    def test_cache_set_and_get(self):
        cache = QgsMapRendererCache()
        img = _make_image(color=0xFF00FF00)
        cache.setCacheImage("layer_a", img)
        result = cache.cacheImage("layer_a")
        assert result is not None
        assert result.width() == 4
        assert result.height() == 4

    def test_cache_miss(self):
        cache = QgsMapRendererCache()
        assert cache.cacheImage("nonexistent") is None

    def test_cache_contains(self):
        cache = QgsMapRendererCache()
        assert cache.contains("layer_a") is False
        cache.setCacheImage("layer_a", _make_image())
        assert cache.contains("layer_a") is True

    def test_cache_count(self):
        cache = QgsMapRendererCache()
        assert cache.count() == 0
        cache.setCacheImage("a", _make_image())
        assert cache.count() == 1
        cache.setCacheImage("b", _make_image())
        assert cache.count() == 2

    def test_cache_overwrite(self):
        cache = QgsMapRendererCache()
        img1 = _make_image(color=0xFFFF0000)
        img2 = _make_image(color=0xFF0000FF)
        cache.setCacheImage("layer_a", img1)
        cache.setCacheImage("layer_a", img2)
        assert cache.count() == 1
        result = cache.cacheImage("layer_a")
        # The overwritten image should be the second one.
        assert result.pixel(0, 0) == 0xFF0000FF


# --------------------------------------------------------------------------- #
#  Invalidation
# --------------------------------------------------------------------------- #
class TestCacheInvalidation:
    def test_cache_invalidate_all(self):
        cache = QgsMapRendererCache()
        cache.setCacheImage("a", _make_image())
        cache.setCacheImage("b", _make_image())
        cache.invalidate()
        assert cache.count() == 0
        assert cache.cacheImage("a") is None
        assert cache.cacheImage("b") is None

    def test_cache_invalidate_one(self):
        cache = QgsMapRendererCache()
        cache.setCacheImage("a", _make_image())
        cache.setCacheImage("b", _make_image())
        cache.invalidateLayer("a")
        assert cache.count() == 1
        assert cache.cacheImage("a") is None
        assert cache.cacheImage("b") is not None


# --------------------------------------------------------------------------- #
#  LRU behaviour
# --------------------------------------------------------------------------- #
class TestCacheLRU:
    def test_cache_lru_eviction(self):
        """When the cache is full the oldest entry is evicted."""
        cache = QgsMapRendererCache()
        cache.setMaxSize(3)
        cache.setCacheImage("a", _make_image(color=0xFFFF0000))
        cache.setCacheImage("b", _make_image(color=0xFF00FF00))
        cache.setCacheImage("c", _make_image(color=0xFF0000FF))
        # Cache is full (3/3). Adding a fourth should evict "a".
        cache.setCacheImage("d", _make_image(color=0xFFFFFF00))
        assert cache.count() == 3
        assert cache.cacheImage("a") is None
        assert cache.cacheImage("b") is not None
        assert cache.cacheImage("c") is not None
        assert cache.cacheImage("d") is not None

    def test_cache_lru_order(self):
        """Accessing an entry moves it to the front, protecting it from eviction."""
        cache = QgsMapRendererCache()
        cache.setMaxSize(3)
        cache.setCacheImage("a", _make_image(color=0xFFFF0000))
        cache.setCacheImage("b", _make_image(color=0xFF00FF00))
        cache.setCacheImage("c", _make_image(color=0xFF0000FF))
        # Touch "a" so it moves to the front of the LRU.
        cache.cacheImage("a")
        # Now add "d" — the oldest *unaccessed* entry ("b") should be evicted.
        cache.setCacheImage("d", _make_image(color=0xFFFFFF00))
        assert cache.cacheImage("a") is not None
        assert cache.cacheImage("b") is None
        assert cache.cacheImage("c") is not None
        assert cache.cacheImage("d") is not None

    def test_cache_set_max_size(self):
        """Resizing the cache downward evicts the oldest entries."""
        cache = QgsMapRendererCache()
        cache.setMaxSize(5)
        for ch in "abcde":
            cache.setCacheImage(ch, _make_image())
        assert cache.count() == 5
        # Shrink to 2 — should evict 3 oldest entries.
        cache.setMaxSize(2)
        assert cache.count() == 2
        # The last two inserted ("d", "e") should survive.
        assert cache.cacheImage("d") is not None
        assert cache.cacheImage("e") is not None

    def test_max_size_default(self):
        cache = QgsMapRendererCache()
        assert cache.maxSize() == 32


# --------------------------------------------------------------------------- #
#  Thread safety
# --------------------------------------------------------------------------- #
class TestCacheThreadSafety:
    def test_cache_thread_safety(self):
        """Concurrent reads and writes must not raise or deadlock."""
        cache = QgsMapRendererCache()
        cache.setMaxSize(64)
        errors: list[Exception] = []

        def writer():
            try:
                for i in range(200):
                    cache.setCacheImage(f"w_{i}", _make_image())
                    if i % 50 == 0:
                        cache.invalidate()
            except Exception as exc:
                errors.append(exc)

        def reader():
            try:
                for i in range(200):
                    cache.cacheImage(f"w_{i}")
                    cache.contains(f"w_{i}")
                    cache.count()
            except Exception as exc:
                errors.append(exc)

        threads = [threading.Thread(target=writer) for _ in range(4)]
        threads += [threading.Thread(target=reader) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10)

        assert errors == [], f"Thread errors: {errors}"
