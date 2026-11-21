#include "bsp_map_view.hpp"
#include "app_language.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace bspvision {

BspMapView::BspMapView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(480, 300);
    setMouseTracking(true);
    setStyleSheet(QStringLiteral("background: #1a1e24;"));
}

void BspMapView::setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons) {
    vertices_ = vertices;
    polygons_ = polygons;
    hasSelection_ = false;

    bool haveBounds = false;
    for (const BspPolygon &polygon : polygons_) {
        for (const BspVertex &vertex : polygon.vertices) {
            if (!haveBounds) {
                minX_ = maxX_ = vertex.x;
                minY_ = maxY_ = vertex.y;
                minZ_ = maxZ_ = vertex.z;
                haveBounds = true;
            } else {
                minX_ = std::min(minX_, static_cast<double>(vertex.x));
                maxX_ = std::max(maxX_, static_cast<double>(vertex.x));
                minY_ = std::min(minY_, static_cast<double>(vertex.y));
                maxY_ = std::max(maxY_, static_cast<double>(vertex.y));
                minZ_ = std::min(minZ_, static_cast<double>(vertex.z));
                maxZ_ = std::max(maxZ_, static_cast<double>(vertex.z));
            }
        }
    }
    if (haveBounds) {
        if (maxX_ - minX_ < 1.0) { minX_ -= 0.5; maxX_ += 0.5; }
        if (maxY_ - minY_ < 1.0) { minY_ -= 0.5; maxY_ += 0.5; }
        if (maxZ_ - minZ_ < 1.0) { minZ_ -= 0.5; maxZ_ += 0.5; }
    } else {
        minX_ = minY_ = minZ_ = 0.0;
        maxX_ = maxY_ = maxZ_ = 1.0;
    }
    update();
}

void BspMapView::setHeightfield(const QImage &hillshade, const QRectF &worldBounds) {
    heightfieldImage_ = hillshade;
    heightfieldWorldBounds_ = worldBounds;
    update();
}

void BspMapView::setHeightfieldContours(const QVector<QLineF> &contours) {
    heightfieldContours_ = contours;
    update();
}

void BspMapView::setHeightfieldVisible(bool visible) {
    heightfieldVisible_ = visible && !heightfieldImage_.isNull();
    update();
}


void BspMapView::setWorldLandmark(double x, double y, bool visible) {
    landmarkWorld_ = QPointF(x, y);
    hasLandmark_ = visible;
    update();
}

void BspMapView::clearMap() {
    vertices_.clear();
    polygons_.clear();
    heightfieldImage_ = {};
    heightfieldWorldBounds_ = {};
    heightfieldContours_.clear();
    heightfieldVisible_ = false;
    hasSelection_ = false;
    hasLandmark_ = false;
    minX_ = minY_ = minZ_ = 0.0;
    maxX_ = maxY_ = maxZ_ = 1.0;
    update();
}

QRectF BspMapView::plotRect() const {
    return QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0);
}

QPointF BspMapView::worldToScreen(double x, double y) const {
    const QRectF plot = plotRect();
    const double spanX = std::max(1.0, maxX_ - minX_);
    const double spanY = std::max(1.0, maxY_ - minY_);
    const double scale = std::min(plot.width() / spanX, plot.height() / spanY);
    const double centerX = (minX_ + maxX_) * 0.5;
    const double centerY = (minY_ + maxY_) * 0.5;
    return QPointF(plot.center().x() + (x - centerX) * scale,
                   plot.center().y() - (y - centerY) * scale);
}

QPointF BspMapView::screenToWorld(const QPointF &point) const {
    const QRectF plot = plotRect();
    const double spanX = std::max(1.0, maxX_ - minX_);
    const double spanY = std::max(1.0, maxY_ - minY_);
    const double scale = std::min(plot.width() / spanX, plot.height() / spanY);
    if (scale <= 0.0) return {};
    const double centerX = (minX_ + maxX_) * 0.5;
    const double centerY = (minY_ + maxY_) * 0.5;
    return QPointF(centerX + (point.x() - plot.center().x()) / scale,
                   centerY - (point.y() - plot.center().y()) / scale);
}

void BspMapView::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(26, 30, 36));

    const QRectF plot = plotRect();
    if (heightfieldVisible_ && !heightfieldImage_.isNull()) {
        const QPointF worldTopLeft = worldToScreen(
            heightfieldWorldBounds_.left(),
            heightfieldWorldBounds_.top() + heightfieldWorldBounds_.height());
        const QPointF worldBottomRight = worldToScreen(
            heightfieldWorldBounds_.left() + heightfieldWorldBounds_.width(),
            heightfieldWorldBounds_.top());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(QRectF(worldTopLeft, worldBottomRight).normalized(), heightfieldImage_);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    painter.setPen(QPen(QColor(48, 55, 64), 1.0));
    for (int i = 1; i < 10; ++i) {
        const qreal x = plot.left() + plot.width() * i / 10.0;
        const qreal y = plot.top() + plot.height() * i / 10.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    painter.setPen(QPen(QColor(72, 82, 96), 1.0));
    painter.drawRect(plot);

    if (polygons_.isEmpty()) {
        painter.setPen(QColor(205, 211, 220));
        painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap,
                         uiText("Load a Source BSP v20 file to preview its top-down geometry.", "Загрузите карту Source BSP v20, чтобы увидеть геометрию сверху."));
        return;
    }

    const double zSpan = std::max(1.0, maxZ_ - minZ_);
    for (const BspPolygon &polygon : polygons_) {
        if (polygon.vertices.size() < 3) continue;
        QPolygonF screenPolygon;
        screenPolygon.reserve(polygon.vertices.size());
        double zAverage = 0.0;
        for (const BspVertex &vertex : polygon.vertices) {
            screenPolygon << worldToScreen(vertex.x, vertex.y);
            zAverage += vertex.z;
        }
        zAverage /= polygon.vertices.size();
        const double level = std::clamp((zAverage - minZ_) / zSpan, 0.0, 1.0);
        const int tint = static_cast<int>(level * 48.0);
        if (heightfieldVisible_) {
            painter.setPen(QPen(QColor(24, 30, 36, 150), 0.45));
            painter.setBrush(Qt::NoBrush);
        } else {
            painter.setPen(QPen(QColor(69 + tint, 83 + tint, 99 + tint), 0.6));
            painter.setBrush(QColor(52 + tint, 66 + tint, 81 + tint));
        }
        painter.drawPolygon(screenPolygon);
    }

    if (heightfieldVisible_ && !heightfieldContours_.isEmpty()) {
        painter.setPen(QPen(QColor(245, 230, 160, 200), 1.0));
        painter.setBrush(Qt::NoBrush);
        for (const QLineF &contour : heightfieldContours_) {
            painter.drawLine(worldToScreen(contour.x1(), contour.y1()),
                             worldToScreen(contour.x2(), contour.y2()));
        }
    }


    if (hasLandmark_) {
        const QPointF landmark = worldToScreen(landmarkWorld_.x(), landmarkWorld_.y());
        painter.setPen(QPen(QColor(255, 205, 75, 245), 2.0));
        painter.setBrush(QColor(255, 205, 75, 75));
        painter.drawEllipse(landmark, 9.0, 9.0);
        painter.drawLine(landmark + QPointF(-15.0, 0.0), landmark + QPointF(15.0, 0.0));
        painter.drawLine(landmark + QPointF(0.0, -15.0), landmark + QPointF(0.0, 15.0));
        painter.setPen(QColor(255, 230, 160));
        painter.drawText(QRectF(landmark.x() + 12.0, landmark.y() - 20.0, 170.0, 20.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         uiText("AIM LANDMARK", "ОРИЕНТИР НАВЕДЕНИЯ"));
    }

    if (hasSelection_) {
        const QPointF point = worldToScreen(selectedWorld_.x(), selectedWorld_.y());
        painter.setPen(QPen(QColor(255, 188, 66), 2.0));
        painter.setBrush(QColor(255, 188, 66, 100));
        painter.drawEllipse(point, 6.0, 6.0);
        painter.drawLine(point + QPointF(-12, 0), point + QPointF(12, 0));
        painter.drawLine(point + QPointF(0, -12), point + QPointF(0, 12));
    }

    painter.setPen(QColor(211, 216, 224));
    painter.drawText(QRectF(12, 4, width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     heightfieldVisible_
                         ? uiText("Hillshade heightfield %1×%2 • cell %3 u • ground/canopy layers",
                                  "Рельеф hillshade %1×%2 • ячейка %3 ед. • слои ground/canopy")
                               .arg(heightfieldImage_.width()).arg(heightfieldImage_.height())
                               .arg(heightfieldWorldBounds_.width() / std::max(1, heightfieldImage_.width()), 0, 'f', 1)
                         : uiText("Top-down XY view • faces: %1 • Z range: %2 to %3",
                                  "Вид сверху XY • граней: %1 • диапазон Z: %2 - %3")
                               .arg(polygons_.size()).arg(minZ_, 0, 'f', 1).arg(maxZ_, 0, 'f', 1));
}

void BspMapView::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || polygons_.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    selectedWorld_ = screenToWorld(event->position());
    hasSelection_ = true;
    update();
    if (pointSelected) pointSelected(selectedWorld_.x(), selectedWorld_.y());
    event->accept();
}

} // namespace bspvision
