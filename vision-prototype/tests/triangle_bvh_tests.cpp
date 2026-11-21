#include "triangle_bvh.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    bspvision::BspPolygon floor;
    floor.materialName = QStringLiteral("concrete/floor");
    floor.vertices = {{0, 0, 0}, {100, 0, 0}, {100, 100, 0}, {0, 100, 0}};

    bspvision::BspPolygon sky;
    sky.skySurface = true;
    sky.vertices = {{0, 0, 5}, {100, 0, 5}, {0, 100, 5}};

    bspvision::TriangleBvh bvh;
    QString error;
    if (!bvh.build({floor, sky}, &error)) {
        std::cerr << "FAIL: valid test geometry rejected: " << error.toStdString() << '\n';
        return 1;
    }
    if (bvh.triangleCount() != 2)
        return 1; // quad triangulates into two; sky triangle is filtered.

    bspvision::SegmentHit hit;
    if (!bvh.intersectSegment(QVector3D(50, 50, 10), QVector3D(50, 50, -10), &hit))
        return 1;
    if (std::abs(hit.position.z()) > 0.001F ||
        std::abs(hit.fraction - 0.5) > 1e-6 ||
        hit.materialName != QStringLiteral("concrete/floor"))
        return 1;

    if (bvh.intersectSegment(QVector3D(150, 150, 10), QVector3D(150, 150, -10), &hit))
        return 1;
    if (bvh.intersectSegment(QVector3D(50, 50, 0), QVector3D(50, 50, 10), &hit))
        return 1; // Touching the launch surface at t=0 is ignored.

    bspvision::TriangleBvh empty;
    if (empty.build({sky}, &error))
        return 1;
    if (!error.contains(QStringLiteral("triangles"), Qt::CaseInsensitive))
        return 1;

    std::cout << "PASS: BVH build, sky filtering, closest finite-segment hit and misses\n";
    return 0;
}
