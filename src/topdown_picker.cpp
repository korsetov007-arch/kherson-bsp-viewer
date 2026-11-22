#include "topdown_picker.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

TopDownPicker::TopDownPicker(QWidget *parent) : QWidget(parent) {
    setMinimumSize(380, 280);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setStyleSheet("background:#0c1009;color:#e8edf2;");
    setToolTip("ЛКМ — выбрать X/Y. Z задаётся отдельно. Стрелка — направление взгляда.");
}

QRectF TopDownPicker::mapRect() const {
    return QRectF(rect()).adjusted(24.0, 34.0, -24.0, -28.0);
}

void TopDownPicker::setPolygons(const QVector<bspvision::BspPolygon> &polygons) {
    polygons_ = polygons;
    bool found = false;
    minX_ = minY_ = std::numeric_limits<double>::max();
    maxX_ = maxY_ = std::numeric_limits<double>::lowest();

    for (const auto &polygon : polygons_) {
        for (const auto &vertex : polygon.vertices) {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) continue;
            minX_ = std::min(minX_, static_cast<double>(vertex.x));
            maxX_ = std::max(maxX_, static_cast<double>(vertex.x));
            minY_ = std::min(minY_, static_cast<double>(vertex.y));
            maxY_ = std::max(maxY_, static_cast<double>(vertex.y));
            found = true;
        }
    }

    if (!found) {
        minX_ = minY_ = -1.0;
        maxX_ = maxY_ = 1.0;
    }
    if (maxX_ - minX_ < 1.0) { minX_ -= 0.5; maxX_ += 0.5; }
    if (maxY_ - minY_ < 1.0) { minY_ -= 0.5; maxY_ += 0.5; }
    hasMarker_ = false;
    trajectory_.clear();
    update();
}

void TopDownPicker::setMarker(double x, double y) {
    markerX_ = x;
    markerY_ = y;
    hasMarker_ = true;
    update();
}

void TopDownPicker::setObserver(double x, double y, double yawDeg, bool valid) {
    observerX_ = x;
    observerY_ = y;
    observerYawDeg_ = yawDeg;
    hasObserver_ = valid;
    update();
}

void TopDownPicker::setTrajectory(const QVector<tfc::Vec3> &points) {
    trajectory_ = points;
    update();
}

void TopDownPicker::setPointCallback(std::function<void(double, double)> callback) {
    pointCallback_ = std::move(callback);
}

QPointF TopDownPicker::worldToScreen(double x, double y) const {
    const QRectF area = mapRect();
    const double spanX = std::max(1.0, maxX_ - minX_);
    const double spanY = std::max(1.0, maxY_ - minY_);
    const double scale = std::min(area.width() / spanX, area.height() / spanY);
    const double mapWidth = spanX * scale;
    const double mapHeight = spanY * scale;
    return {
        area.center().x() - mapWidth * 0.5 + (x - minX_) * scale,
        area.center().y() - mapHeight * 0.5 + (maxY_ - y) * scale
    };
}

QPointF TopDownPicker::screenToWorld(const QPointF &point) const {
    const QRectF area = mapRect();
    const double spanX = std::max(1.0, maxX_ - minX_);
    const double spanY = std::max(1.0, maxY_ - minY_);
    const double scale = std::min(area.width() / spanX, area.height() / spanY);
    const double mapWidth = spanX * scale;
    const double mapHeight = spanY * scale;
    if (scale <= 0.0) return {};
    return {
        minX_ + (point.x() - (area.center().x() - mapWidth * 0.5)) / scale,
        maxY_ - (point.y() - (area.center().y() - mapHeight * 0.5)) / scale
    };
}

void TopDownPicker::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(12, 16, 9));

    painter.setPen(QColor(164, 166, 154));
    painter.drawText(QRect(14, 7, width() - 28, 20), Qt::AlignLeft | Qt::AlignVCenter,
        "TACTICAL MAP  /  X-Y  ·  ЛКМ — точка цели  ·  LIVE — машина и взгляд");

    const QRectF area = mapRect();
    painter.fillRect(area, QColor(8, 10, 7));
    painter.setPen(QPen(QColor(42, 48, 36), 1.0));
    for (int i = 1; i < 10; ++i) {
        const double x = area.left() + area.width() * i / 10.0;
        const double y = area.top() + area.height() * i / 10.0;
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    for (const auto &polygon : polygons_) {
        if (polygon.vertices.size() < 3) continue;
        QPolygonF shape;
        shape.reserve(polygon.vertices.size());
        for (const auto &vertex : polygon.vertices)
            shape << worldToScreen(vertex.x, vertex.y);
        const bool terrain = polygon.displacementSurface;
        painter.setPen(QPen(terrain ? QColor(104, 132, 102) : QColor(88, 104, 83), 0.55));
        painter.setBrush(terrain ? QColor(67, 91, 67, 96) : QColor(76, 83, 64, 70));
        painter.drawPolygon(shape);
    }

    if (trajectory_.size() > 1) {
        painter.setPen(QPen(QColor(225, 166, 83), 2.0, Qt::DashLine,
                            Qt::RoundCap, Qt::RoundJoin));
        for (qsizetype i = 1; i < trajectory_.size(); ++i)
            painter.drawLine(worldToScreen(trajectory_[i - 1].x, trajectory_[i - 1].y),
                             worldToScreen(trajectory_[i].x, trajectory_[i].y));
    }

    painter.setPen(QPen(QColor(73, 84, 66), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(area);

    if (hasObserver_) {
        const QPointF center = worldToScreen(observerX_, observerY_);
        const double angle = observerYawDeg_ * 3.14159265358979323846 / 180.0;
        const QPointF direction(std::cos(angle), -std::sin(angle));
        const QPointF tip = center + direction * 29.0;
        const QPointF side(-direction.y(), direction.x());
        QPolygonF arrow;
        arrow << tip << (tip - direction * 10.0 + side * 5.0)
              << (tip - direction * 10.0 - side * 5.0);
        painter.setPen(QPen(QColor(157, 190, 123), 2.0, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(center, tip);
        painter.setBrush(QColor(157, 190, 123));
        painter.drawPolygon(arrow);
        painter.setBrush(QColor(12, 16, 9));
        painter.setPen(QPen(QColor(209, 224, 182), 2.0));
        painter.drawEllipse(center, 7.0, 7.0);
        painter.setPen(QColor(174, 195, 142));
        painter.drawText(center + QPointF(12.0, -11.0), "LIVE");
    }

    if (hasMarker_) {
        const QPointF center = worldToScreen(markerX_, markerY_);
        painter.setPen(QPen(QColor(255, 178, 76), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(center + QPointF(-10.0, 0.0), center + QPointF(10.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -10.0), center + QPointF(0.0, 10.0));
        painter.drawEllipse(center, 5.0, 5.0);
    }
}

void TopDownPicker::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !polygons_.isEmpty() &&
        mapRect().contains(event->position())) {
        const QPointF world = screenToWorld(event->position());
        markerX_ = world.x();
        markerY_ = world.y();
        hasMarker_ = true;
        update();
        if (pointCallback_) pointCallback_(markerX_, markerY_);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
