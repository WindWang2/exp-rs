"""Thread-safe LRU cache for per-layer rendered QImages.

Mirrors the QGIS ``QgsMapRendererCache`` interface using Python's
``collections.OrderedDict`` for O(1) LRU operations and a
``threading.Lock`` for concurrent access safety.
"""

from __future__ import annotations

import threading
from collections import OrderedDict

from PySide6.QtGui import QImage


class QgsMapRendererCache:
    """LRU cache storing ``QImage`` objects keyed by layer id (str).

    Default maximum size is **32** entries.  When the limit is reached the
    least-recently-used entry is evicted automatically.
    """

    _DEFAULT_MAX_SIZE = 32

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._cache: OrderedDict[str, QImage] = OrderedDict()
        self._max_size: int = self._DEFAULT_MAX_SIZE

    # ------------------------------------------------------------------ #
    #  Public API
    # ------------------------------------------------------------------ #

    def setCacheImage(self, layer_id: str, image: QImage) -> None:
        """Store *image* for *layer_id*, evicting the oldest entry if full.

        If *layer_id* already exists its value is updated and it moves to the
        front of the LRU list.
        """
        with self._lock:
            if layer_id in self._cache:
                # Move to end (most-recently-used) and update value.
                self._cache.move_to_end(layer_id)
                self._cache[layer_id] = image
            else:
                self._cache[layer_id] = image
                self._evict_if_needed()

    def cacheImage(self, layer_id: str) -> QImage | None:
        """Return the cached image for *layer_id*, or ``None`` on miss.

        A successful lookup promotes the entry to the front of the LRU.
        """
        with self._lock:
            if layer_id not in self._cache:
                return None
            self._cache.move_to_end(layer_id)
            return self._cache[layer_id]

    def invalidate(self) -> None:
        """Remove **all** cached images."""
        with self._lock:
            self._cache.clear()

    def invalidateLayer(self, layer_id: str) -> None:
        """Remove the cached image for a single *layer_id* (no-op if absent)."""
        with self._lock:
            self._cache.pop(layer_id, None)

    def setMaxSize(self, n: int) -> None:
        """Change the maximum number of cached entries.

        If the cache currently exceeds *n* entries, the oldest are evicted
        immediately.
        """
        with self._lock:
            self._max_size = max(0, n)
            self._evict_if_needed()

    def maxSize(self) -> int:
        """Return the current maximum cache size."""
        with self._lock:
            return self._max_size

    def count(self) -> int:
        """Return the number of entries currently in the cache."""
        with self._lock:
            return len(self._cache)

    def contains(self, layer_id: str) -> bool:
        """Return ``True`` if *layer_id* is present in the cache."""
        with self._lock:
            return layer_id in self._cache

    # ------------------------------------------------------------------ #
    #  Internal helpers
    # ------------------------------------------------------------------ #

    def _evict_if_needed(self) -> None:
        """Evict least-recently-used entries until within the size limit.

        **Must be called while holding ``self._lock``.**
        """
        while len(self._cache) > self._max_size:
            self._cache.popitem(last=False)
