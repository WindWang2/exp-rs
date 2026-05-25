#include "antigravity_init.h"
#include <qgscoordinatereferencesystem.h>
#include <qgsrasterlayer.h>
#include <qgsmapsettings.h>
#include <qgsmaprenderersequentialjob.h>
#include <QImage>
#include <cstdio>
#include <cstdlib>

int main() {
    antigravity_init(qgetenv("ANTIGRAVITY_DATA"));

    // Task 6: CRS smoke
    QgsCoordinateReferenceSystem crs("EPSG:32650");
    if (!crs.isValid()) { fprintf(stderr, "FAIL CRS invalid\n"); return 1; }
    fprintf(stderr, "PASS CRS ok: %s\n", crs.authid().toUtf8().constData());

    // Task 7: render LE7 B4 off-thread to QImage
    {
        const QString tif = qgetenv("ANTIGRAVITY_DATA") + "/../data/LE7/LE71300411999327EDC00_B4.TIF";
        QgsRasterLayer *layer = new QgsRasterLayer(tif, "b4", "gdal");
        if (!layer->isValid()) {
            fprintf(stderr, "FAIL layer invalid: %s\n", tif.toUtf8().constData());
            return 2;
        }
        QgsMapSettings settings;
        settings.setLayers({layer});
        settings.setExtent(layer->extent());
        settings.setOutputSize(QSize(512, 512));
        settings.setDestinationCrs(layer->crs());
        QgsMapRendererSequentialJob job(settings);
        job.start();
        job.waitForFinished();
        QImage img = job.renderedImage();
        delete layer;
        if (img.isNull() || img.size() != QSize(512, 512)) {
            fprintf(stderr, "FAIL render empty\n");
            return 3;
        }
        const QString outPath = qgetenv("ANTIGRAVITY_DATA") + "/../cmake-build/le7_cpp_render.png";
        img.save(outPath);
        fprintf(stderr, "PASS render ok: %s\n", outPath.toUtf8().constData());
    }

    fprintf(stderr, "ALL PASS — exiting cleanly\n");
    fflush(stderr);
    // _exit skips global destructors, avoiding the QGIS/PROJ teardown crash
    // (QgsCoordinateTransformPrivate::freeProj during DSO __do_global_dtors_aux).
    _Exit(0);
}
