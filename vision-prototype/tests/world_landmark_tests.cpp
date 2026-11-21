#include "world_landmark.hpp"

#include <QPointF>
#include <QSize>
#include <QVector3D>

#include <cmath>
#include <iostream>

namespace {
bool near(double a, double b, double tolerance = 0.05) {
    return std::abs(a - b) <= tolerance;
}
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}

int main() {
    const QSize viewport(1920, 1080);
    QPointF point;
    double depth = 0.0;
    bool ok = true;

    ok &= check(bspvision::projectWorldPointToScreen(
        QVector3D(100, 0, 0), QVector3D(0, 0, 0),
        0.0, 0.0, 90.0, viewport, &point, &depth),
        "Front point was not projected.");
    ok &= check(near(point.x(), 960.0) && near(point.y(), 540.0) && near(depth, 100.0),
        "Front point did not land at viewport center.");

    ok &= check(bspvision::projectWorldPointToScreen(
        QVector3D(100, 25, 0), QVector3D(0, 0, 0),
        0.0, 0.0, 90.0, viewport, &point),
        "Side point was not projected.");
    ok &= check(point.x() < 960.0,
        "Positive world Y should appear on the screen's left for Source yaw.");

    ok &= check(bspvision::projectWorldPointToScreen(
        QVector3D(0, 100, 0), QVector3D(0, 0, 0),
        90.0, 0.0, 90.0, viewport, &point),
        "Rotated point was not projected.");
    ok &= check(near(point.x(), 960.0) && near(point.y(), 540.0),
        "Yaw rotation did not bring the +Y landmark to screen center.");

    ok &= check(bspvision::projectWorldPointToScreen(
        QVector3D(100, 0, 10), QVector3D(0, 0, 0),
        0.0, 10.0, 90.0, viewport, &point),
        "Elevated point was not projected.");
    ok &= check(point.y() < 540.0,
        "A point above the camera should appear above screen center.");

    ok &= check(!bspvision::projectWorldPointToScreen(
        QVector3D(-100, 0, 0), QVector3D(0, 0, 0),
        0.0, 0.0, 90.0, viewport, &point),
        "A point behind the camera was incorrectly projected.");

    ok &= check(!bspvision::projectWorldPointToScreen(
        QVector3D(100, 0, 0), QVector3D(0, 0, 0),
        0.0, 0.0, 90.0, QSize(0, 1080), &point),
        "Invalid viewport dimensions should be rejected.");

    return ok ? 0 : 1;
}
