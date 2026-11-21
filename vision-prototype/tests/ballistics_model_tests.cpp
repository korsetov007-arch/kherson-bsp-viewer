#include "ballistics_model.hpp"

#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}
}

int main() {
    using namespace bspvision;
    bool ok = true;
    const QVector3D origin(120.0f, -80.0f, 32.0f);
    const QVector3D target(10120.0f, 4920.0f, 132.0f);

    const BallisticSolution low = solveBallisticTarget(origin, target, 4000.0, 600.0, false);
    ok &= check(low.isValid(), "The reachable target did not produce a low-arc solution.");
    if (low.isValid()) {
        ok &= check(near(low.yawRadians, std::atan2(5000.0, 10000.0), 1.0e-5),
                    "The azimuth does not point from launch to target.");
        ok &= check(low.pitchRadians > 0.0 && low.pitchRadians < 0.5,
                    "The low arc elevation is outside the expected range.");
        ok &= check(low.endpointError < 0.1,
                    "The low-arc solution does not terminate at the target.");
        const auto points = sampleBallisticArc(origin, 4000.0, 600.0,
            low.yawRadians, low.pitchRadians, low.flightSeconds);
        ok &= check(points.size() >= 121, "The trajectory was not sampled densely enough.");
        if (!points.isEmpty()) {
            const QVector3D end = points.last();
            ok &= check((end - target).length() < 1.0,
                        "The sampled low arc does not reach the target.");
        }
    }

    const BallisticSolution high = solveBallisticTarget(origin, target, 4000.0, 600.0, true);
    ok &= check(high.isValid(), "The reachable target did not produce a high-arc solution.");
    if (low.isValid() && high.isValid()) {
        ok &= check(high.pitchRadians > low.pitchRadians,
                    "The high arc is not steeper than the low arc.");
        ok &= check(high.flightSeconds > low.flightSeconds,
                    "The high arc should take longer than the low arc.");
    }

    const BallisticSolution unreachable = solveBallisticTarget(
        QVector3D(0, 0, 0), QVector3D(50000, 0, 0), 4000.0, 600.0);
    ok &= check(unreachable.error == BallisticSolveError::OutOfRange,
                "An out-of-range target was not rejected.");

    const BallisticSolution noGravity = solveBallisticTarget(
        QVector3D(0, 0, 0), QVector3D(300, 400, 120), 100.0, 0.0);
    ok &= check(noGravity.isValid(), "Zero-gravity direct solution failed.");
    if (noGravity.isValid()) {
        ok &= check(near(noGravity.flightSeconds, std::sqrt(300.0 * 300.0 +
                    400.0 * 400.0 + 120.0 * 120.0) / 100.0, 1.0e-4),
                    "Zero-gravity flight time is incorrect.");
    }

    const BallisticSolution invalid = solveBallisticTarget(
        origin, target, 0.0, 600.0);
    ok &= check(invalid.error == BallisticSolveError::InvalidInput,
                "A zero-speed projectile was not rejected.");

    const BallisticSolution sameXY = solveBallisticTarget(
        QVector3D(0, 0, 0), QVector3D(0, 0, 20), 4000.0, 600.0);
    ok &= check(sameXY.error == BallisticSolveError::CoincidentPoints,
                "A target with no horizontal separation was not rejected.");

    return ok ? 0 : 1;
}
