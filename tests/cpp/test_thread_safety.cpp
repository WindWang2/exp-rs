#include "antigravity_init.h"
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrasterlayer.h>
#include <QtConcurrent>
#include <QVector>
#include <atomic>
#include <cstdio>
#include <cstdlib>

int main() {
    antigravity_init(qgetenv("ANTIGRAVITY_DATA"));
    QgsCoordinateReferenceSystem src("EPSG:4326"), dst("EPSG:3857");
    QVector<int> work(64);
    std::atomic<int> ok{0};
    QtConcurrent::blockingMap(work, [&](int &) {
        QgsCoordinateTransform ct(src, dst, QgsCoordinateTransformContext());
        QgsPointXY p = ct.transform(QgsPointXY(116.4, 39.9));
        QgsRasterLayer layer(qgetenv("ANTIGRAVITY_DATA") + "/../data/LE7/LE71300411999327EDC00_B4.TIF", "r", "gdal");
        if (layer.isValid() && p.x() != 0.0) ok++;
    });
    int n = ok.load();
    fprintf(stderr, "thread-safe transforms ok: %d/64\n", n);
    fflush(stderr);
    _Exit(n == 64 ? 0 : 1);
}
