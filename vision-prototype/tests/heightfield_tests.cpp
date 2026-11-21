#include "heightfield.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    bspvision::BspPolygon ground;
    ground.vertices = {{0, 0, 0}, {128, 0, 0}, {0, 128, 0}};

    bspvision::BspPolygon roof;
    roof.vertices = {{32, 32, 64}, {96, 32, 64}, {32, 96, 64}};

    bspvision::Heightfield heightfield;
    QString error;
    if (!heightfield.build({ground, roof}, 8.0, 64, &error)) {
        std::cerr << "FAIL: valid heightfield rejected: " << error.toStdString() << '\n';
        return 1;
    }
    if (heightfield.width() != 16 || heightfield.height() != 16)
        return 1;
    if (heightfield.coveredCellCount() <= 0)
        return 1;
    if (heightfield.hillshadeImage().size() != QSize(16, 16))
        return 1;

    const float baseZ = heightfield.groundAt(44, 44);
    const float topZ = heightfield.canopyAt(44, 44);
    if (!std::isfinite(baseZ) || !std::isfinite(topZ) ||
        std::abs(baseZ - 0.0F) > 0.01F || std::abs(topZ - 64.0F) > 0.01F) {
        std::cerr << "FAIL: two-layer heights were not preserved at an overlapping cell.\n";
        return 1;
    }

    bspvision::BspPolygon slope;
    slope.vertices = {{0, 0, 0}, {128, 0, 256}, {0, 128, 0}};
    bspvision::Heightfield sloped;
    if (!sloped.build({slope}, 8.0, 64, &error)) {
        std::cerr << "FAIL: sloped heightfield rejected: " << error.toStdString() << '\\n';
        return 1;
    }
    if (sloped.contours128().isEmpty())
        return 1;
    for (const QLineF &line : sloped.contours128()) {
        if (!std::isfinite(line.x1()) || !std::isfinite(line.y1()) ||
            !std::isfinite(line.x2()) || !std::isfinite(line.y2()))
            return 1;
    }

    QString invalidError;
    if (heightfield.build({}, 16.0, 64, &invalidError))
        return 1;
    if (!invalidError.contains(QStringLiteral("geometry"), Qt::CaseInsensitive))
        return 1;

    std::cout << "PASS: heightfield rasterization, ground/canopy layers and hillshade image\n";
    return 0;
}
