#include "bsp_3d_view.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bspvision {
namespace {
struct RenderFace {
    QPolygonF polygon;
    double depth = 0.0;
    double verticality = 0.0;
    double height = 0.0;
};

struct RotatedPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

RotatedPoint rotatePoint(const BspVertex &vertex, double centerX, double centerY,
                         double centerZ, double yaw, double pitch) {
    const double x = static_cast<double>(vertex.x) - centerX;
    const double y = static_cast<double>(vertex.y) - centerY;
    const double z = static_cast<double>(vertex.z) - centerZ;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double x1 = cy * x - sy * y;
    const double y1 = sy * x + cy * y;
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    return {x1, cp * y1 - sp * z, sp * y1 + cp * z};
}
} // namespace

Bsp3DView::Bsp3DView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(420, 280);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setStyleSheet(QStringLiteral("background: #1a1e24;"));
}

void Bsp3DView::setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons) {
    vertices_ = vertices;
    polygons_ = polygons;
    yaw_ = 0.65;
    pitch_ = 0.72;
    zoom_ = 1.0;
    dragging_ = false;

    if (vertices_.isEmpty()) {
        centerX_ = centerY_ = centerZ_ = 0.0;
        minZ_ = 0.0;
        maxZ_ = 1.0;
        mapDiameter_ = 1.0;
        update();
        return;
    }

    double minX = vertices_.first().x;
    double maxX = minX;
    double minY = vertices_.first().y;
    double maxY = minY;
    minZ_ = maxZ_ = vertices_.first().z;
    for (const BspVertex &vertex : vertices_) {
        minX = std::min(minX, static_cast<double>(vertex.x));
        maxX = std::max(maxX, static_cast<double>(vertex.x));
        minY = std::min(minY, static_cast<double>(vertex.y));
        maxY = std::max(maxY, static_cast<double>(vertex.y));
        minZ_ = std::min(minZ_, static_cast<double>(vertex.z));
        maxZ_ = std::max(maxZ_, static_cast<double>(vertex.z));
    }
    centerX_ = (minX + maxX) * 0.5;
    centerY_ = (minY + maxY) * 0.5;
    centerZ_ = (minZ_ + maxZ_) * 0.5;
    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    const double spanZ = maxZ_ - minZ_;
    mapDiameter_ = std::max(1.0, std::sqrt(spanX * spanX + spanY * spanY + spanZ * spanZ));
    update();
}

void Bsp3DView::clearMap() {
    vertices_.clear();
    polygons_.clear();
    centerX_ = centerY_ = centerZ_ = 0.0;
    minZ_ = 0.0;
    maxZ_ = 1.0;
    mapDiameter_ = 1.0;
    dragging_ = false;
    update();
}

QRectF Bsp3DView::viewportRect() const {
    return QRectF(rect()).adjusted(18.0, 24.0, -18.0, -18.0);
}

void Bsp3DView::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(26, 30, 36));

    const QRectF viewport = viewportRect();
    painter.setPen(QPen(QColor(48, 55, 64), 1.0));
    for (int i = 1; i < 8; ++i) {
        const qreal x = viewport.left() + viewport.width() * i / 8.0;
        const qreal y = viewport.top() + viewport.height() * i / 8.0;
        painter.drawLine(QPointF(x, viewport.top()), QPointF(x, viewport.bottom()));
        painter.drawLine(QPointF(viewport.left(), y), QPointF(viewport.right(), y));
    }
    painter.setPen(QPen(QColor(72, 82, 96), 1.0));
    painter.drawRect(viewport);

    if (vertices_.isEmpty() || polygons_.isEmpty()) {
        painter.setPen(QColor(205, 211, 220));
        painter.drawText(viewport, Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("Load a Source BSP v20 file to inspect its 3D face geometry."));
        return;
    }

    const double scale = std::min(viewport.width(), viewport.height()) /
                         (mapDiameter_ * 1.12) * zoom_;
    std::vector<RenderFace> renderFaces;
    renderFaces.reserve(static_cast<size_t>(polygons_.size()));
    const double zSpan = std::max(1.0, maxZ_ - minZ_);

    for (const BspPolygon &face : polygons_) {
        if (face.vertices.size() < 3) continue;
        std::vector<RotatedPoint> rotated;
        rotated.reserve(static_cast<size_t>(face.vertices.size()));
        RenderFace renderFace;
        double zAverage = 0.0;
        double depthAverage = 0.0;
        for (const BspVertex &vertex : face.vertices) {
            const RotatedPoint point = rotatePoint(
                vertex, centerX_, centerY_, centerZ_, yaw_, pitch_);
            rotated.push_back(point);
            renderFace.polygon << QPointF(viewport.center().x() + point.x * scale,
                                          viewport.center().y() - point.z * scale);
            zAverage += vertex.z;
            depthAverage += point.y;
        }
        renderFace.depth = depthAverage / static_cast<double>(rotated.size());
        renderFace.height = (zAverage / static_cast<double>(rotated.size()) - minZ_) / zSpan;

        const BspVertex &a = face.vertices[0];
        const BspVertex &b = face.vertices[1];
        const BspVertex &c = face.vertices[2];
        const double abx = static_cast<double>(b.x) - a.x;
        const double aby = static_cast<double>(b.y) - a.y;
        const double abz = static_cast<double>(b.z) - a.z;
        const double acx = static_cast<double>(c.x) - a.x;
        const double acy = static_cast<double>(c.y) - a.y;
        const double acz = static_cast<double>(c.z) - a.z;
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        const double normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
        renderFace.verticality = normalLength > 0.0 ? std::abs(nz) / normalLength : 0.0;
        renderFaces.push_back(std::move(renderFace));
    }

    // A painter-based depth order gives a useful interactive overview without requiring OpenGL.
    // It is not a z-buffer renderer; intersecting and back-facing surfaces can overlap imperfectly.
    std::sort(renderFaces.begin(), renderFaces.end(),
              [](const RenderFace &left, const RenderFace &right) {
                  return left.depth < right.depth;
              });

    for (const RenderFace &face : renderFaces) {
        const double shade = std::clamp(
            62.0 + 76.0 * face.verticality + 22.0 * std::clamp(face.height, 0.0, 1.0),
            40.0, 170.0);
        const int r = static_cast<int>(shade * 0.76);
        const int g = static_cast<int>(shade * 0.88);
        const int b = static_cast<int>(shade);
        painter.setPen(QPen(QColor(std::min(255, r + 18), std::min(255, g + 18),
                                   std::min(255, b + 18)), 0.6));
        painter.setBrush(QColor(r, g, b));
        painter.drawPolygon(face.polygon);
    }

    painter.setPen(QColor(211, 216, 224));
    painter.drawText(QRectF(10, 3, width() - 20, 18), Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Orbit 3D • drag to rotate • mouse wheel to zoom • %1 faces")
            .arg(renderFaces.size()));
}

void Bsp3DView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !vertices_.isEmpty()) {
        dragging_ = true;
        lastMousePosition_ = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void Bsp3DView::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_) {
        const QPoint current = event->position().toPoint();
        const QPoint delta = current - lastMousePosition_;
        lastMousePosition_ = current;
        yaw_ += delta.x() * 0.012;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.012, -1.48, 1.48);
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void Bsp3DView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Bsp3DView::wheelEvent(QWheelEvent *event) {
    if (!vertices_.isEmpty()) {
        const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
        zoom_ = std::clamp(zoom_ * std::pow(1.12, steps), 0.2, 8.0);
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

} // namespace bspvision
