#pragma once

#include "bsp_file.hpp"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>

namespace bspvision {

class BspMapView final : public QWidget {
public:
    explicit BspMapView(QWidget *parent = nullptr);

    void setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons);
    void setHeightfield(const QImage &hillshade, const QRectF &worldBounds);
    void setHeightfieldVisible(bool visible);
    void clearMap();

    std::function<void(double, double)> pointSelected;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF plotRect() const;
    QPointF worldToScreen(double x, double y) const;
    QPointF screenToWorld(const QPointF &point) const;

    QVector<BspVertex> vertices_;
    QVector<BspPolygon> polygons_;
    QImage heightfieldImage_;
    QRectF heightfieldWorldBounds_;
    bool heightfieldVisible_ = false;
    QPointF selectedWorld_;
    bool hasSelection_ = false;
    double minX_ = 0.0;
    double maxX_ = 1.0;
    double minY_ = 0.0;
    double maxY_ = 1.0;
    double minZ_ = 0.0;
    double maxZ_ = 1.0;
};

} // namespace bspvision
