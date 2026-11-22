#include "ballistics.hpp"

#include <cassert>
#include <cmath>

namespace {
constexpr double kEpsilon = 1e-5;

bool near(double a, double b) {
    return std::abs(a - b) < kEpsilon;
}
}

int main() {
    // World yaw follows Source XY: 0=+X, +90=+Y, -90=-Y.
    const auto east = tfc::solveLowArc({0, 0, 0}, {1000, 0, 0}, 4000, 600);
    const auto north = tfc::solveLowArc({0, 0, 0}, {0, 1000, 0}, 4000, 600);
    const auto south = tfc::solveLowArc({0, 0, 0}, {0, -1000, 0}, 4000, 600);
    assert(east && north && south);
    assert(near(east->worldYawDeg, 0.0));
    assert(near(north->worldYawDeg, 90.0));
    assert(near(south->worldYawDeg, -90.0));

    // Source view pitch is negative for an upward shot, positive for a downward shot.
    const auto uphill = tfc::solveLowArc({0, 0, 0}, {1000, 0, 100}, 4000, 600);
    const auto downhill = tfc::solveLowArc({0, 0, 100}, {1000, 0, 0}, 4000, 600);
    assert(uphill && downhill);
    assert(uphill->viewPitchDeg < 0.0);
    assert(downhill->viewPitchDeg > 0.0);

    assert(std::isfinite(east->viewPitchDeg));
    assert(east->flightTimeSec > 0.0);
    assert(!tfc::solveLowArc({0, 0, 0}, {1000000, 0, 0}, 100, 600));
    assert(!tfc::solveLowArc({0, 0, 0}, {100, 0, 0}, 0, 600));
    assert(!tfc::solveLowArc({0, 0, 0}, {100, 0, 0}, 4000, 0));
    return 0;
}
