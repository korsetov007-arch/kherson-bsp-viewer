#pragma once

#include "bsp_file.hpp"

#include <QWidget>
#include <QVector3D>

namespace bspvision {

class BallisticsView final : public QWidget {
public:
    explicit BallisticsView(QWidget *parent = nullptr);
    void setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons);
    void setTrajectory(const QVector<QVector3D> &points, const QVector3D &origin,
                       const QVector3D &target, bool hasTarget);
    void clearTrajectory();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<BspVertex> vertices_;
    QVector<BspPolygon> polygons_;
    QVector<QVector3D> trajectory_;
    QVector3D origin_;
    QVector3D target_;
    bool hasTarget_ = false;
};

} // namespace bspvision
