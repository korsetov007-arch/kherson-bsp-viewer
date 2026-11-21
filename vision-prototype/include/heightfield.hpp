#pragma once

#include "bsp_file.hpp"

#include <QImage>
#include <QLineF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace bspvision {

// Two height samples per XY cell: the lowest and highest upward-facing
// world surfaces. Missing samples are represented by NaN.
class Heightfield final {
public:
    bool build(const QVector<BspPolygon> &polygons,
               double requestedCellSize = 16.0,
               int maxDimension = 2048,
               QString *error = nullptr);
    void clear();

    bool isValid() const { return width_ > 0 && height_ > 0 && !hillshade_.isNull(); }
    int width() const { return width_; }
    int height() const { return height_; }
    int coveredCellCount() const { return coveredCellCount_; }
    double cellSize() const { return cellSize_; }
    double minX() const { return minX_; }
    double maxX() const { return maxX_; }
    double minY() const { return minY_; }
    double maxY() const { return maxY_; }
    QRectF worldBounds() const { return QRectF(minX_, minY_, maxX_ - minX_, maxY_ - minY_); }

    const QVector<float> &groundLayer() const { return ground_; }
    const QVector<float> &canopyLayer() const { return canopy_; }
    const QImage &hillshadeImage() const { return hillshade_; }
    const QVector<QLineF> &contours128() const { return contours128_; }
    const QVector<QLineF> &contours256() const { return contours256_; }

    float groundAt(double x, double y) const;
    float canopyAt(double x, double y) const;

private:
    void buildContours(double interval, QVector<QLineF> *output) const;
    qsizetype cellIndexAt(double x, double y) const;

    int width_ = 0;
    int height_ = 0;
    int coveredCellCount_ = 0;
    double cellSize_ = 0.0;
    double minX_ = 0.0;
    double maxX_ = 0.0;
    double minY_ = 0.0;
    double maxY_ = 0.0;
    QVector<float> ground_;
    QVector<float> canopy_;
    QVector<QLineF> contours128_;
    QVector<QLineF> contours256_;
    QImage hillshade_;
};

} // namespace bspvision
