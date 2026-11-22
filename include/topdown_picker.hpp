#pragma once

#include "ballistics.hpp"
#include "bsp_file.hpp"

#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include <functional>

class QPaintEvent;
class QMouseEvent;

class TopDownPicker final : public QWidget {
public:
    explicit TopDownPicker(QWidget *parent = nullptr);

    void setPolygons(const QVector<bspvision::BspPolygon> &polygons);
    void setMarker(double x, double y);
    void setObserver(double x, double y, double yawDeg, bool valid);
    void setTrajectory(const QVector<tfc::Vec3> &points);
    void setPointCallback(std::function<void(double, double)> callback);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF mapRect() const;
    QPointF worldToScreen(double x, double y) const;
    QPointF screenToWorld(const QPointF &point) const;

    QVector<bspvision::BspPolygon> polygons_;
    QVector<tfc::Vec3> trajectory_;
    double minX_ = -1.0;
    double maxX_ = 1.0;
    double minY_ = -1.0;
    double maxY_ = 1.0;
    double markerX_ = 0.0;
    double markerY_ = 0.0;
    double observerX_ = 0.0;
    double observerY_ = 0.0;
    double observerYawDeg_ = 0.0;
    bool hasMarker_ = false;
    bool hasObserver_ = false;
    std::function<void(double, double)> pointCallback_;
};
