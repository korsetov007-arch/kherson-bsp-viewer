#pragma once

#include <QVector>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace bspvision {

enum class BallisticSolveError {
    None,
    InvalidInput,
    CoincidentPoints,
    OutOfRange,
    InvalidSolution
};

struct BallisticSolution {
    BallisticSolveError error = BallisticSolveError::InvalidInput;
    double yawRadians = 0.0;
    double pitchRadians = 0.0;
    double flightSeconds = 0.0;
    double endpointError = 0.0;
    bool highArc = false;

    bool isValid() const { return error == BallisticSolveError::None; }
};

// Solves an ideal, constant-speed projectile between two Source-world points.
// Positive pitch is elevation (up); yaw is measured in the XY plane.
inline BallisticSolution solveBallisticTarget(const QVector3D &origin,
                                             const QVector3D &target,
                                             double speed,
                                             double gravity,
                                             bool highArc = false) {
    BallisticSolution solution;
    solution.highArc = highArc;

    const double values[] = {
        origin.x(), origin.y(), origin.z(),
        target.x(), target.y(), target.z(),
        speed, gravity
    };
    for (double value : values) {
        if (!std::isfinite(value)) {
            solution.error = BallisticSolveError::InvalidInput;
            return solution;
        }
    }
    if (speed <= 0.0 || gravity < 0.0) {
        solution.error = BallisticSolveError::InvalidInput;
        return solution;
    }

    const double dx = static_cast<double>(target.x()) - origin.x();
    const double dy = static_cast<double>(target.y()) - origin.y();
    const double dz = static_cast<double>(target.z()) - origin.z();
    const double range = std::hypot(dx, dy);
    if (range < 0.01) {
        solution.error = BallisticSolveError::CoincidentPoints;
        return solution;
    }

    solution.yawRadians = std::atan2(dy, dx);
    if (gravity <= 1.0e-9) {
        solution.pitchRadians = std::atan2(dz, range);
        const double straightDistance = std::hypot(range, dz);
        solution.flightSeconds = straightDistance / speed;
    } else {
        const double speedSquared = speed * speed;
        const double speedFourth = speedSquared * speedSquared;
        const double gravityTerm = gravity *
            (gravity * range * range + 2.0 * dz * speedSquared);
        double discriminant = speedFourth - gravityTerm;
        const double tolerance = 1.0e-10 *
            std::max({1.0, std::abs(speedFourth), std::abs(gravityTerm)});
        if (discriminant < -tolerance) {
            solution.error = BallisticSolveError::OutOfRange;
            return solution;
        }
        discriminant = std::max(0.0, discriminant);

        const double root = std::sqrt(discriminant);
        const double denominator = gravity * range;
        const double tangent = (speedSquared + (highArc ? root : -root)) / denominator;
        solution.pitchRadians = std::atan(tangent);
        const double horizontalSpeed = speed * std::cos(solution.pitchRadians);
        if (!std::isfinite(horizontalSpeed) || horizontalSpeed <= 1.0e-6) {
            solution.error = BallisticSolveError::InvalidSolution;
            return solution;
        }
        solution.flightSeconds = range / horizontalSpeed;
    }

    if (!std::isfinite(solution.yawRadians) ||
        !std::isfinite(solution.pitchRadians) ||
        !std::isfinite(solution.flightSeconds) ||
        solution.flightSeconds <= 0.0) {
        solution.error = BallisticSolveError::InvalidSolution;
        return solution;
    }

    const double horizontalSpeed = speed * std::cos(solution.pitchRadians);
    const double verticalSpeed = speed * std::sin(solution.pitchRadians);
    const double endX = origin.x() + horizontalSpeed * std::cos(solution.yawRadians) * solution.flightSeconds;
    const double endY = origin.y() + horizontalSpeed * std::sin(solution.yawRadians) * solution.flightSeconds;
    const double endZ = origin.z() + verticalSpeed * solution.flightSeconds -
                        0.5 * gravity * solution.flightSeconds * solution.flightSeconds;
    solution.endpointError = std::sqrt(
        (endX - target.x()) * (endX - target.x()) +
        (endY - target.y()) * (endY - target.y()) +
        (endZ - target.z()) * (endZ - target.z()));
    if (!std::isfinite(solution.endpointError)) {
        solution.error = BallisticSolveError::InvalidSolution;
        return solution;
    }

    solution.error = BallisticSolveError::None;
    return solution;
}

inline QVector<QVector3D> sampleBallisticArc(const QVector3D &origin,
                                             double speed,
                                             double gravity,
                                             double yawRadians,
                                             double pitchRadians,
                                             double durationSeconds,
                                             int samplesPerSecond = 24) {
    QVector<QVector3D> points;
    if (!std::isfinite(speed) || !std::isfinite(gravity) ||
        !std::isfinite(yawRadians) || !std::isfinite(pitchRadians) ||
        !std::isfinite(durationSeconds) || speed <= 0.0 ||
        gravity < 0.0 || durationSeconds <= 0.0) {
        return points;
    }

    const double requestedSteps = std::ceil(durationSeconds *
        static_cast<double>(std::clamp(samplesPerSecond, 1, 240)));
    const int steps = static_cast<int>(std::clamp(requestedSteps, 120.0, 2400.0));
    const double horizontalSpeed = speed * std::cos(pitchRadians);
    const double vx = horizontalSpeed * std::cos(yawRadians);
    const double vy = horizontalSpeed * std::sin(yawRadians);
    const double vz = speed * std::sin(pitchRadians);
    points.reserve(steps + 1);

    for (int i = 0; i <= steps; ++i) {
        const double t = durationSeconds * static_cast<double>(i) / steps;
        points.push_back(QVector3D(
            static_cast<float>(origin.x() + vx * t),
            static_cast<float>(origin.y() + vy * t),
            static_cast<float>(origin.z() + vz * t - 0.5 * gravity * t * t)));
    }
    return points;
}

} // namespace bspvision
