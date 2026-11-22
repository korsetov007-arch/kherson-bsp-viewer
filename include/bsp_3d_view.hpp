#pragma once

#include "bsp_file.hpp"

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>

namespace bspvision {

class Bsp3DView final : public QWidget {
public:
    explicit Bsp3DView(QWidget *parent = nullptr);

    void setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons);
    void clearMap();
    void setTarget(double x, double y, double z);
    void clearTarget();
    void setTargetCallback(std::function<void(double, double, double)> callback);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF viewportRect() const;
    bool pickTarget(const QPointF &screenPosition, double &x, double &y, double &z) const;

    QVector<BspVertex> vertices_;
    QVector<BspPolygon> polygons_;
    std::function<void(double, double, double)> targetCallback_;
    double centerX_ = 0.0;
    double centerY_ = 0.0;
    double centerZ_ = 0.0;
    double minZ_ = 0.0;
    double maxZ_ = 1.0;
    double mapDiameter_ = 1.0;
    double yaw_ = 0.65;
    double pitch_ = 0.72;
    double zoom_ = 1.0;
    double targetX_ = 0.0;
    double targetY_ = 0.0;
    double targetZ_ = 0.0;
    QPoint lastMousePosition_;
    QPoint pressMousePosition_;
    bool dragging_ = false;
    bool hasTarget_ = false;
};

} // namespace bspvision
