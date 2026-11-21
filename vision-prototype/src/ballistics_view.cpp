#include "ballistics_view.hpp"
#include "app_language.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace bspvision {
namespace {
struct Bounds {
    double minX = -100.0, maxX = 100.0;
    double minY = -100.0, maxY = 100.0;
    double minZ = -50.0, maxZ = 150.0;
};

QPointF project(const QVector3D &p, const QRectF &r, const Bounds &b, int mode) {
    const double sx = std::max(1.0, b.maxX - b.minX);
    const double sy = std::max(1.0, b.maxY - b.minY);
    const double sz = std::max(1.0, b.maxZ - b.minZ);
    const double nx = (p.x() - b.minX) / sx;
    const double ny = (p.y() - b.minY) / sy;
    const double nz = (p.z() - b.minZ) / sz;
    if (mode == 0) return {r.left() + nx * r.width(), r.bottom() - ny * r.height()};
    if (mode == 1) return {r.left() + nx * r.width(), r.bottom() - nz * r.height()};
    const double px = (nx - 0.5) * 0.82 + (ny - 0.5) * 0.42;
    const double py = (ny - 0.5) * 0.26 - (nz - 0.5) * 0.86;
    return {r.center().x() + px * r.width(), r.center().y() + py * r.height()};
}

QVector3D asVector(const BspVertex &v) { return {v.x, v.y, v.z}; }
}

BallisticsView::BallisticsView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(720, 460);
    setStyleSheet(QStringLiteral("background: #1a1e24;"));
}

void BallisticsView::setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons) {
    vertices_ = vertices;
    polygons_ = polygons;
    update();
}

void BallisticsView::setTrajectory(const QVector<QVector3D> &points, const QVector3D &origin,
                                   const QVector3D &target, bool hasTarget) {
    trajectory_ = points;
    origin_ = origin;
    target_ = target;
    hasTarget_ = hasTarget;
    update();
}

void BallisticsView::clearTrajectory() {
    trajectory_.clear();
    hasTarget_ = false;
    update();
}

void BallisticsView::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(26, 30, 36));

    Bounds b;
    bool haveBounds = false;
    auto include = [&b, &haveBounds](const QVector3D &v) {
        if (!haveBounds) {
            b.minX = b.maxX = v.x(); b.minY = b.maxY = v.y(); b.minZ = b.maxZ = v.z();
            haveBounds = true;
        } else {
            b.minX = std::min(b.minX, static_cast<double>(v.x()));
            b.maxX = std::max(b.maxX, static_cast<double>(v.x()));
            b.minY = std::min(b.minY, static_cast<double>(v.y()));
            b.maxY = std::max(b.maxY, static_cast<double>(v.y()));
            b.minZ = std::min(b.minZ, static_cast<double>(v.z()));
            b.maxZ = std::max(b.maxZ, static_cast<double>(v.z()));
        }
    };
    for (const BspPolygon &face : polygons_)
        for (const BspVertex &v : face.vertices) include(asVector(v));
    for (const auto &v : trajectory_) include(v);
    if (!haveBounds) { b = {}; }
    const double padX = std::max(32.0, (b.maxX - b.minX) * 0.06);
    const double padY = std::max(32.0, (b.maxY - b.minY) * 0.06);
    const double padZ = std::max(32.0, (b.maxZ - b.minZ) * 0.12);
    b.minX -= padX; b.maxX += padX;
    b.minY -= padY; b.maxY += padY;
    b.minZ -= padZ; b.maxZ += padZ;

    const qreal gap = 12.0;
    const qreal top = 34.0;
    const qreal panelW = (width() - gap * 4.0) / 3.0;
    const qreal panelH = height() - top - gap * 2.0;
    const QRectF panels[3] = {
        QRectF(gap, top, panelW, panelH),
        QRectF(gap * 2 + panelW, top, panelW, panelH),
        QRectF(gap * 3 + panelW * 2, top, panelW, panelH)
    };
    const QString titles[3] = {uiText("XY · PLAN", "XY · ПЛАН"), uiText("XZ · ELEVATION", "XZ · ВИД СБОКУ"),
                               uiText("3D · TRAJECTORY", "3D · ТРАЕКТОРИЯ")};
    const QString axes[3] = {QStringLiteral("X →   Y ↑"), QStringLiteral("X →   Z ↑"),
                             QStringLiteral("X / Y / Z")};

    p.setPen(QColor(222, 227, 235));
    p.drawText(QRectF(12, 5, width() - 24, 24), Qt::AlignLeft | Qt::AlignVCenter,
        uiText("BALLISTIC LAB  ·  world units  ·  green = launch, red = target, amber = path", "БАЛЛИСТИКА  ·  единицы мира  ·  зелёный = старт, красный = цель, янтарный = траектория"));

    for (int mode = 0; mode < 3; ++mode) {
        const QRectF panel = panels[mode];
        p.fillRect(panel, QColor(31, 36, 44));
        p.setPen(QPen(QColor(65, 74, 86), 1.0));
        p.drawRect(panel);
        p.setPen(QColor(174, 184, 198));
        p.drawText(QRectF(panel.left() + 8, panel.top() + 4, panel.width() - 16, 20),
                   Qt::AlignLeft | Qt::AlignVCenter, titles[mode]);
        const QRectF plot = panel.adjusted(12, 30, -12, -28);
        p.setPen(QPen(QColor(49, 57, 68), 1.0));
        for (int i = 1; i < 6; ++i) {
            const qreal fraction = static_cast<qreal>(i) / 6.0;
            const qreal x = plot.left() + plot.width() * fraction;
            const qreal y = plot.top() + plot.height() * fraction;
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            if (mode < 2) {
                const double xValue = b.minX + (b.maxX - b.minX) * fraction;
                const double verticalMin = mode == 0 ? b.minY : b.minZ;
                const double verticalMax = mode == 0 ? b.maxY : b.maxZ;
                const double verticalValue = verticalMin + (verticalMax - verticalMin) * (1.0 - fraction);
                p.setPen(QColor(133, 146, 163));
                p.drawText(QRectF(x - 24, plot.bottom() - 17, 48, 14),
                           Qt::AlignHCenter | Qt::AlignVCenter,
                           QString::number(xValue, 'f', 0));
                p.drawText(QRectF(plot.left() + 1, y - 7, 42, 14),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QString::number(verticalValue, 'f', 0));
                p.setPen(QPen(QColor(49, 57, 68), 1.0));
            }
        }
        p.setPen(QColor(132, 145, 162));
        p.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width(), 18),
                   Qt::AlignLeft | Qt::AlignVCenter, axes[mode]);

        if (mode == 0 || mode == 2) {
            p.setPen(QPen(QColor(78, 91, 106, 145), 0.8));
            for (const auto &face : polygons_) {
                if (face.vertices.size() < 2) continue;
                QPolygonF poly;
                for (const auto &v : face.vertices) poly << project(asVector(v), plot, b, mode);
                p.drawPolyline(poly);
            }
        }

        if (trajectory_.size() >= 2) {
            QPolygonF path;
            for (const auto &point : trajectory_) path << project(point, plot, b, mode);
            p.setPen(QPen(QColor(255, 190, 75), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(path);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 190, 75));
            for (qsizetype i = 4; i < trajectory_.size() - 1;
                 i += std::max<qsizetype>(1, trajectory_.size() / 12))
                p.drawEllipse(project(trajectory_[i], plot, b, mode), 2.8, 2.8);
        }

        const QPointF originPt = project(origin_, plot, b, mode);
        p.setPen(QPen(QColor(76, 224, 144), 2.0));
        p.setBrush(QColor(76, 224, 144));
        p.drawEllipse(originPt, 5.0, 5.0);
        p.drawLine(originPt + QPointF(-9, 0), originPt + QPointF(9, 0));
        p.drawLine(originPt + QPointF(0, -9), originPt + QPointF(0, 9));
        if (hasTarget_) {
            const QPointF targetPt = project(target_, plot, b, mode);
            p.setPen(QPen(QColor(255, 98, 105), 2.0));
            p.setBrush(QColor(255, 98, 105));
            p.drawEllipse(targetPt, 5.0, 5.0);
            p.drawLine(targetPt + QPointF(-7, -7), targetPt + QPointF(7, 7));
            p.drawLine(targetPt + QPointF(-7, 7), targetPt + QPointF(7, -7));
        }
    }

    if (trajectory_.isEmpty()) {
        p.setPen(QColor(220, 225, 233));
        p.drawText(rect().adjusted(20, 40, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
            uiText("Set launch coordinates and speed, then calculate or solve for a target. The three panels show top-down XY, side elevation XZ, and an oblique 3D projection.",
                   "Задайте координаты старта и скорость, затем рассчитайте траекторию или решите задачу для цели. Три панели показывают XY сверху, XZ сбоку и наклонную 3D-проекцию."));
    }
}

} // namespace bspvision
