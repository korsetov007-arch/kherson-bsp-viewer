#pragma once

#include "bsp_file.hpp"

#include <QPoint>
#include <QRectF>
#include <QWidget>

namespace bspvision {

class Bsp3DView final : public QWidget {
public:
    explicit Bsp3DView(QWidget *parent = nullptr);

    void setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons);
    void clearMap();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF viewportRect() const;

    QVector<BspVertex> vertices_;
    QVector<BspPolygon> polygons_;
    double centerX_ = 0.0;
    double centerY_ = 0.0;
    double centerZ_ = 0.0;
    double minZ_ = 0.0;
    double maxZ_ = 1.0;
    double mapDiameter_ = 1.0;
    double yaw_ = 0.65;
    double pitch_ = 0.72;
    double zoom_ = 1.0;
    QPoint lastMousePosition_;
    bool dragging_ = false;
};

} // namespace bspvision
