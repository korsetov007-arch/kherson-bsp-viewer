#pragma once

#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace bspvision {

struct WorldLandmark {
    QString name;
    QVector3D position;
};

// Projects a Source-world point into the game viewport using cl_showpos angles.
// Source yaw=0 looks along +X; increasing yaw looks toward +Y (the screen's left).
// Source positive pitch looks downward.
inline bool projectWorldPointToScreen(const QVector3D &worldPoint,
                                      const QVector3D &cameraPosition,
                                      double yawDegrees,
                                      double sourcePitchDegrees,
                                      double horizontalFovDegrees,
                                      const QSize &viewport,
                                      QPointF *screenPoint,
                                      double *cameraDepth = nullptr) {
    if (!screenPoint || viewport.width() <= 0 || viewport.height() <= 0)
        return false;

    const double values[] = {
        worldPoint.x(), worldPoint.y(), worldPoint.z(),
        cameraPosition.x(), cameraPosition.y(), cameraPosition.z(),
        yawDegrees, sourcePitchDegrees, horizontalFovDegrees
    };
    for (double value : values) {
        if (!std::isfinite(value)) return false;
    }

    const double fov = std::clamp(horizontalFovDegrees, 1.0, 179.0);
    const double yaw = yawDegrees * 3.14159265358979323846 / 180.0;
    const double pitch = sourcePitchDegrees * 3.14159265358979323846 / 180.0;
    const double cosYaw = std::cos(yaw);
    const double sinYaw = std::sin(yaw);
    const double cosPitch = std::cos(pitch);
    const double sinPitch = std::sin(pitch);

    const double forwardX = cosPitch * cosYaw;
    const double forwardY = cosPitch * sinYaw;
    const double forwardZ = -sinPitch;
    const double rightX = sinYaw;
    const double rightY = -cosYaw;
    const double upX = cosYaw * sinPitch;
    const double upY = sinYaw * sinPitch;
    const double upZ = cosPitch;

    const double dx = static_cast<double>(worldPoint.x()) - cameraPosition.x();
    const double dy = static_cast<double>(worldPoint.y()) - cameraPosition.y();
    const double dz = static_cast<double>(worldPoint.z()) - cameraPosition.z();

    const double depth = dx * forwardX + dy * forwardY + dz * forwardZ;
    if (cameraDepth) *cameraDepth = depth;
    if (!std::isfinite(depth) || depth <= 0.01) return false;

    const double right = dx * rightX + dy * rightY;
    const double up = dx * upX + dy * upY + dz * upZ;
    const double halfFovTangent = std::tan(fov * 3.14159265358979323846 / 360.0);
    if (!std::isfinite(halfFovTangent) || halfFovTangent <= 0.0) return false;

    const double focalX = static_cast<double>(viewport.width()) / (2.0 * halfFovTangent);
    const double verticalHalfFovTangent = halfFovTangent *
        static_cast<double>(viewport.height()) / static_cast<double>(viewport.width());
    const double focalY = static_cast<double>(viewport.height()) /
                          (2.0 * verticalHalfFovTangent);
    const double x = static_cast<double>(viewport.width()) * 0.5 + focalX * right / depth;
    const double y = static_cast<double>(viewport.height()) * 0.5 - focalY * up / depth;
    if (!std::isfinite(x) || !std::isfinite(y)) return false;

    *screenPoint = QPointF(x, y);
    return true;
}

} // namespace bspvision
