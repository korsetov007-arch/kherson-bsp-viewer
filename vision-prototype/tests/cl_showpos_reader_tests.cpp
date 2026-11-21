#include "cl_showpos_reader.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {
int fail(const char *message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bspvision::ClShowPosSample sample;
    QString error;

    if (!bspvision::ClShowPosReader::parseTextLines({
            QStringLiteral("pos:  123.45 -67.89  256.00"),
            QStringLiteral("ang:  -12.50  90.00  0.00"),
            QStringLiteral("vel:  123.45")
        }, &sample, &error)) {
        std::cerr << "FAIL: valid telemetry was rejected: " << error.toStdString() << '\n';
        return 1;
    }
    if (std::abs(sample.position.x() - 123.45F) > 0.01F ||
        std::abs(sample.position.y() + 67.89F) > 0.01F ||
        std::abs(sample.position.z() - 256.0F) > 0.01F)
        return fail("position was parsed incorrectly");
    if (std::abs(sample.angles.x() + 12.5F) > 0.01F ||
        std::abs(sample.angles.y() - 90.0F) > 0.01F ||
        std::abs(sample.angles.z()) > 0.01F)
        return fail("Source pitch/yaw/roll was parsed incorrectly");
    if (!sample.hasSpeed || std::abs(sample.speed - 123.45) > 0.01)
        return fail("optional velocity was not parsed");

    if (bspvision::ClShowPosReader::parseTextLines({
            QStringLiteral("pos: 1 2 3"),
            QStringLiteral("ang: nan 0 0")
        }, &sample, &error))
        return fail("non-numeric angle token was accepted");

    if (bspvision::ClShowPosReader::parseTextLines({
            QStringLiteral("pos: 999999999999 0 0"),
            QStringLiteral("ang: 0 0 0")
        }, &sample, &error))
        return fail("implausible world coordinate was accepted");

    if (bspvision::ClShowPosReader::parseTextLines({
            QStringLiteral("pos: 1 2 3")
        }, &sample, &error))
        return fail("missing angle line was accepted");

    std::cout << "PASS: cl_showpos text parsing, Source angle order, optional velocity and range checks\n";
    return 0;
}
