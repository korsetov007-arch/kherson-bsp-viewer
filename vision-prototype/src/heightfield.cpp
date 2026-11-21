#include "heightfield.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bspvision {
namespace {

struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

bool finiteVertex(const BspVertex &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double cross2D(const Point3 &a, const Point3 &b, const Point3 &c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

} // namespace

void Heightfield::clear() {
    width_ = 0;
    height_ = 0;
    coveredCellCount_ = 0;
    cellSize_ = 0.0;
    minX_ = maxX_ = minY_ = maxY_ = 0.0;
    ground_.clear();
    canopy_.clear();
    hillshade_ = {};
}

bool Heightfield::build(const QVector<BspPolygon> &polygons,
                        double requestedCellSize,
                        int maxDimension,
                        QString *error) {
    clear();
    if (error) error->clear();
    if (!(requestedCellSize > 0.0) || !std::isfinite(requestedCellSize) ||
        maxDimension < 16 || maxDimension > 8192) {
        if (error) *error = QStringLiteral("Cell size must be positive and max dimension must be between 16 and 8192.");
        return false;
    }

    bool haveBounds = false;
    for (const BspPolygon &polygon : polygons) {
        if (polygon.skySurface || polygon.vertices.size() < 3) continue;
        for (const BspVertex &vertex : polygon.vertices) {
            if (!finiteVertex(vertex)) continue;
            if (!haveBounds) {
                minX_ = maxX_ = vertex.x;
                minY_ = maxY_ = vertex.y;
                haveBounds = true;
            } else {
                minX_ = std::min(minX_, static_cast<double>(vertex.x));
                maxX_ = std::max(maxX_, static_cast<double>(vertex.x));
                minY_ = std::min(minY_, static_cast<double>(vertex.y));
                maxY_ = std::max(maxY_, static_cast<double>(vertex.y));
            }
        }
    }
    if (!haveBounds || maxX_ - minX_ < 1e-3 || maxY_ - minY_ < 1e-3) {
        clear();
        if (error) *error = QStringLiteral("No usable XY geometry was found for the heightfield.");
        return false;
    }

    const double spanX = maxX_ - minX_;
    const double spanY = maxY_ - minY_;
    cellSize_ = std::max(requestedCellSize,
                         std::max(spanX / maxDimension, spanY / maxDimension));
    width_ = std::clamp(static_cast<int>(std::ceil(spanX / cellSize_)), 1, maxDimension);
    height_ = std::clamp(static_cast<int>(std::ceil(spanY / cellSize_)), 1, maxDimension);
    if (width_ <= 0 || height_ <= 0) {
        clear();
        if (error) *error = QStringLiteral("Could not determine heightfield dimensions.");
        return false;
    }

    const qsizetype cellCount = static_cast<qsizetype>(width_) * height_;
    const float infinity = std::numeric_limits<float>::infinity();
    ground_.fill(infinity, cellCount);
    canopy_.fill(-infinity, cellCount);

    // Rasterize upward-facing triangles. A polygon fan is sufficient for the
    // mostly convex BSP face outlines; displacement surfaces arrive as triangles.
    for (const BspPolygon &polygon : polygons) {
        if (polygon.skySurface || polygon.vertices.size() < 3) continue;
        const BspVertex &rootVertex = polygon.vertices[0];
        if (!finiteVertex(rootVertex)) continue;
        const Point3 aRoot{rootVertex.x, rootVertex.y, rootVertex.z};

        for (qsizetype tri = 1; tri + 1 < polygon.vertices.size(); ++tri) {
            const BspVertex &vb = polygon.vertices[tri];
            const BspVertex &vc = polygon.vertices[tri + 1];
            if (!finiteVertex(vb) || !finiteVertex(vc)) continue;
            const Point3 a = aRoot;
            const Point3 b{vb.x, vb.y, vb.z};
            const Point3 c{vc.x, vc.y, vc.z};
            const double denom = cross2D(a, b, c);
            // Positive projected winding corresponds to an upward-facing plane.
            // Ignore vertical, downward-facing and degenerate triangles.
            if (denom <= 1e-5) continue;

            const double triMinX = std::min({a.x, b.x, c.x});
            const double triMaxX = std::max({a.x, b.x, c.x});
            const double triMinY = std::min({a.y, b.y, c.y});
            const double triMaxY = std::max({a.y, b.y, c.y});

            const int minCol = std::clamp(static_cast<int>(std::floor((triMinX - minX_) / cellSize_)),
                                          0, width_ - 1);
            const int maxCol = std::clamp(static_cast<int>(std::floor((triMaxX - minX_) / cellSize_)),
                                          0, width_ - 1);
            const int minRow = std::clamp(static_cast<int>(std::floor((maxY_ - triMaxY) / cellSize_)),
                                          0, height_ - 1);
            const int maxRow = std::clamp(static_cast<int>(std::floor((maxY_ - triMinY) / cellSize_)),
                                          0, height_ - 1);

            for (int row = minRow; row <= maxRow; ++row) {
                const double worldY = maxY_ - (static_cast<double>(row) + 0.5) * cellSize_;
                for (int col = minCol; col <= maxCol; ++col) {
                    const double worldX = minX_ + (static_cast<double>(col) + 0.5) * cellSize_;

                    const double wa = ((b.y - c.y) * (worldX - c.x) +
                                       (c.x - b.x) * (worldY - c.y)) / denom;
                    const double wb = ((c.y - a.y) * (worldX - c.x) +
                                       (a.x - c.x) * (worldY - c.y)) / denom;
                    const double wc = 1.0 - wa - wb;
                    constexpr double epsilon = 1e-7;
                    if (wa < -epsilon || wb < -epsilon || wc < -epsilon) continue;

                    const float z = static_cast<float>(wa * a.z + wb * b.z + wc * c.z);
                    const qsizetype index = static_cast<qsizetype>(row) * width_ + col;
                    ground_[index] = std::min(ground_[index], z);
                    canopy_[index] = std::max(canopy_[index], z);
                }
            }
        }
    }

    coveredCellCount_ = 0;
    for (qsizetype i = 0; i < cellCount; ++i) {
        if (std::isfinite(ground_[i])) ++coveredCellCount_;
        else {
            ground_[i] = std::numeric_limits<float>::quiet_NaN();
            canopy_[i] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    if (coveredCellCount_ == 0) {
        clear();
        if (error) *error = QStringLiteral("No upward-facing triangles could be rasterized. Check face winding and displacement geometry.");
        return false;
    }

    hillshade_ = QImage(width_, height_, QImage::Format_ARGB32);
    if (hillshade_.isNull()) {
        clear();
        if (error) *error = QStringLiteral("Not enough memory to build the hillshade image.");
        return false;
    }
    hillshade_.fill(qRgba(0, 0, 0, 0));

    const auto sampleHeight = [this](int row, int col, float fallback) {
        if (row < 0 || row >= height_ || col < 0 || col >= width_) return static_cast<double>(fallback);
        const float value = ground_[static_cast<qsizetype>(row) * width_ + col];
        return std::isfinite(value) ? static_cast<double>(value) : static_cast<double>(fallback);
    };

    constexpr double lightX = -0.45;
    constexpr double lightY = -0.40;
    constexpr double lightZ = 0.80;
    const double lightLength = std::sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);
    for (int row = 0; row < height_; ++row) {
        QRgb *pixels = reinterpret_cast<QRgb *>(hillshade_.scanLine(row));
        for (int col = 0; col < width_; ++col) {
            const float h = ground_[static_cast<qsizetype>(row) * width_ + col];
            if (!std::isfinite(h)) continue;
            const double dx = (sampleHeight(row, col + 1, h) - sampleHeight(row, col - 1, h)) /
                              (2.0 * cellSize_);
            const double dy = (sampleHeight(row - 1, col, h) - sampleHeight(row + 1, col, h)) /
                              (2.0 * cellSize_);
            double nx = -dx, ny = -dy, nz = 1.0;
            const double normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (normalLength > 0.0) { nx /= normalLength; ny /= normalLength; nz /= normalLength; }
            const double dot = std::max(0.0, (nx * lightX + ny * lightY + nz * lightZ) / lightLength);
            const int shade = std::clamp(static_cast<int>(45.0 + 200.0 * (0.28 + 0.72 * dot)), 40, 235);
            pixels[col] = qRgba(shade, static_cast<int>(shade * 0.98),
                                static_cast<int>(shade * 0.91), 225);
        }
    }

    if (error) error->clear();
    return true;
}

qsizetype Heightfield::cellIndexAt(double x, double y) const {
    if (!isValid() || !std::isfinite(x) || !std::isfinite(y) ||
        x < minX_ || x > maxX_ || y < minY_ || y > maxY_)
        return -1;
    const int col = std::clamp(static_cast<int>((x - minX_) / cellSize_), 0, width_ - 1);
    const int row = std::clamp(static_cast<int>((maxY_ - y) / cellSize_), 0, height_ - 1);
    return static_cast<qsizetype>(row) * width_ + col;
}

float Heightfield::groundAt(double x, double y) const {
    const qsizetype index = cellIndexAt(x, y);
    return index >= 0 ? ground_[index] : std::numeric_limits<float>::quiet_NaN();
}

float Heightfield::canopyAt(double x, double y) const {
    const qsizetype index = cellIndexAt(x, y);
    return index >= 0 ? canopy_[index] : std::numeric_limits<float>::quiet_NaN();
}

} // namespace bspvision
