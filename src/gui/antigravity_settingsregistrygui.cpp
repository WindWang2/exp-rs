// ANTIGRAVITY: Replacement for settings/qgssettingsregistrygui.cpp.
// The original file includes qgscodeeditor.h (Qsci) and Qwt headers which are
// not available in this build. This stub defines all static members and
// provides a no-op constructor (migration copyValueFromKey calls are skipped).

#include "settings/qgssettingsregistrygui.h"
#include "qgssettingstree.h"
#include "qgssettings.h"
#include "qgssettingsentryimpl.h"
#include "qgssettingsentryenumflag.h"
#include "qgsabstractgeometry.h"

#include <QColor>
#include <QString>

using namespace Qt::StringLiterals;

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsRespectScreenDPI =
    new QgsSettingsEntryBool( u"respect-screen-dpi"_s, QgsSettingsTree::sTreeGui, false );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsCadFloaterActive =
    new QgsSettingsEntryBool( u"floater-active"_s, QgsSettingsTree::sTreeCad, false, u"Whether the CAD floater widget is active"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsRasterHistogramShowMarkers =
    new QgsSettingsEntryBool( u"show-markers"_s, QgsSettingsTree::sTreeRasterHistogram, false, u"Whether to show markers on the raster histogram"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsRasterHistogramZoomToMinMax =
    new QgsSettingsEntryBool( u"zoom-to-min-max"_s, QgsSettingsTree::sTreeRasterHistogram, false, u"Whether to zoom the raster histogram to min/max values"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsRasterHistogramUpdateStyleToMinMax =
    new QgsSettingsEntryBool( u"update-style-to-min-max"_s, QgsSettingsTree::sTreeRasterHistogram, true, u"Whether to update the style when histogram changes to min/max values"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsRasterHistogramDrawLines =
    new QgsSettingsEntryBool( u"draw-lines"_s, QgsSettingsTree::sTreeRasterHistogram, true, u"Whether to draw the raster histogram as lines"_s );

const QgsSettingsEntryDouble *QgsSettingsRegistryGui::settingsZoomFactor =
    new QgsSettingsEntryDouble( u"zoom-factor"_s, QgsSettingsTree::sTreeQgis, 2.0, u"Zoom factor for map canvas and other views"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsReverseWheelZoom =
    new QgsSettingsEntryBool( u"reverse-wheel-zoom"_s, QgsSettingsTree::sTreeQgis, false, u"Whether to reverse the direction of wheel zoom"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsNewLayersVisible =
    new QgsSettingsEntryBool( u"new-layers-visible"_s, QgsSettingsTree::sTreeQgis, true, u"Whether newly added layers are visible by default"_s );

const QgsSettingsEntryString *QgsSettingsRegistryGui::settingsRasterDefaultPalette =
    new QgsSettingsEntryString( u"default-palette"_s, QgsSettingsTree::sTreeRaster, QString(), u"Default color ramp palette name for raster layers"_s );

const QgsSettingsEntryInteger *QgsSettingsRegistryGui::settingsMessageTimeout =
    new QgsSettingsEntryInteger( u"message-timeout"_s, QgsSettingsTree::sTreeQgis, 5, u"Timeout in seconds for message bar messages"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsEnableAntiAliasing =
    new QgsSettingsEntryBool( u"enable-anti-aliasing"_s, QgsSettingsTree::sTreeQgis, true, u"Whether anti-aliasing is enabled for rendering"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsNativeColorDialogs =
    new QgsSettingsEntryBool( u"native-color-dialogs"_s, QgsSettingsTree::sTreeQgis, false, u"Whether to use native color dialogs"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsFormatLayerName =
    new QgsSettingsEntryBool( u"format-layer-name"_s, QgsSettingsTree::sTreeQgis, false, u"Whether to format layer names for better readability"_s );

const QgsSettingsEntryBool *QgsSettingsRegistryGui::settingsOpenSublayersInGroup =
    new QgsSettingsEntryBool( u"open-sublayers-in-group"_s, QgsSettingsTree::sTreeQgis, false, u"Whether to open sublayers in a group"_s );

const QgsSettingsEntryInteger *QgsSettingsRegistryGui::settingsMapUpdateInterval =
    new QgsSettingsEntryInteger( u"map-update-interval"_s, QgsSettingsTree::sTreeQgis, 250, u"Map update interval in milliseconds"_s );

const QgsSettingsEntryDouble *QgsSettingsRegistryGui::settingsMagnifierFactorDefault =
    new QgsSettingsEntryDouble( u"magnifier-factor-default"_s, QgsSettingsTree::sTreeQgis, 1.0, u"Default magnifier factor"_s );

const QgsSettingsEntryDouble *QgsSettingsRegistryGui::settingsSegmentationTolerance =
    new QgsSettingsEntryDouble( u"segmentation-tolerance"_s, QgsSettingsTree::sTreeQgis, 0.01745, u"Segmentation tolerance for curved geometries"_s );

const QgsSettingsEntryColor *QgsSettingsRegistryGui::settingsDefaultMeasureColor =
    new QgsSettingsEntryColor( u"default-measure-color"_s, QgsSettingsTree::sTreeQgis, QColor( 222, 155, 67 ), u"Default measure tool color"_s );

const QgsSettingsEntryEnumFlag<QgsAbstractGeometry::SegmentationToleranceType> *QgsSettingsRegistryGui::settingsSegmentationToleranceType =
    new QgsSettingsEntryEnumFlag<QgsAbstractGeometry::SegmentationToleranceType>(
        u"segmentation-tolerance-type"_s, QgsSettingsTree::sTreeQgis,
        QgsAbstractGeometry::MaximumAngle, u"Segmentation tolerance type for curved geometries"_s );

QgsSettingsRegistryGui::QgsSettingsRegistryGui()
    : QgsSettingsRegistry()
{
    // Migration of old settings keys is skipped in the ANTIGRAVITY build
    // (no QgsCodeEditor, QgsHistogramWidget, QgsGradientColorRampDialog, etc.)
}

QgsSettingsRegistryGui::~QgsSettingsRegistryGui()
{}
