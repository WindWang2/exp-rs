"""QgsVectorLayerLabelProvider -- centroid-based label renderer.

Draws text labels at feature centroids using settings from
QgsPalLayerSettings.  No collision detection; each feature's label
is rendered independently.
"""

from __future__ import annotations

from PySide6.QtGui import QPainter, QTextOption

from core.labeling.qgspallabeling import QgsPalLayerSettings
from core.qgsfeature import QgsFeature
from core.qgsrendercontext import QgsRenderContext


class QgsVectorLayerLabelProvider:
    """Simple centroid-based label provider.

    Retrieves label text from a feature attribute and draws it at the
    feature's centroid position on the paint device.
    """

    __slots__ = ('_settings',)

    def __init__(self, settings: QgsPalLayerSettings) -> None:
        self._settings = settings

    # ------------------------------------------------------------------
    # Accessors
    # ------------------------------------------------------------------

    def settings(self) -> QgsPalLayerSettings:
        """Return the label settings for this provider."""
        return self._settings

    def isActive(self) -> bool:
        """Return True if labeling is enabled."""
        return self._settings.enabled

    # ------------------------------------------------------------------
    # Label text extraction
    # ------------------------------------------------------------------

    def labelForFeature(self, feature: QgsFeature) -> str:
        """Return the label string for *feature*.

        Looks up the attribute value by ``settings().fieldName`` and
        returns its string representation (empty string if the field is
        not set or the attribute is not found).
        """
        field = self._settings.fieldName
        if not field:
            return ""
        value = feature.attribute(field)
        if value is None:
            return ""
        return str(value)

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------

    def renderLabels(
        self,
        feature: QgsFeature,
        painter: QPainter,
        renderContext: QgsRenderContext,
    ) -> None:
        """Draw the label for *feature* at its centroid.

        Steps:
        1. Extract label text from the feature attribute.
        2. Compute the centroid of the feature geometry.
        3. Convert centroid to device coordinates via render context.
        4. Apply font/color from settings and draw.
        """
        if not self.isActive():
            return

        text = self.labelForFeature(feature)
        if not text:
            return

        geom = feature.geometry()
        if geom.isNull() or geom.isEmpty():
            return

        # --- centroid extraction ---
        centroid_geom = geom.centroid()
        if not centroid_geom.isNull() and not centroid_geom.isEmpty():
            centroid_pt = centroid_geom.asPoint()
        else:
            bb = geom.boundingBox()
            from core.qgspointxy import QgsPointXY
            centroid_pt = QgsPointXY(
                (bb.xMinimum() + bb.xMaximum()) / 2.0,
                (bb.yMinimum() + bb.yMaximum()) / 2.0,
            )

        # --- map to device coordinates ---
        mtp = renderContext.mapToPixel()
        device_x, device_y = mtp.transform(centroid_pt.x(), centroid_pt.y())

        # --- configure painter ---
        painter.setFont(self._settings.font)
        painter.setPen(self._settings.color)
        painter.drawText(
            device_x,
            device_y,
            text,
        )
