#include "bsp_3d_view.hpp"
#include "app_language.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace bspvision {
namespace {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 operator+(const Vec3 &a, const Vec3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 operator-(const Vec3 &a, const Vec3 &b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 operator*(const Vec3 &v, double scale) {
    return {v.x * scale, v.y * scale, v.z * scale};
}
double dot(const Vec3 &a, const Vec3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
double magnitude(const Vec3 &v) {
    return std::sqrt(dot(v, v));
}
Vec3 normalized(const Vec3 &v) {
    const double length = magnitude(v);
    return length > 1e-12 ? v * (1.0 / length) : Vec3{0.0, 0.0, 1.0};
}
Vec3 toVec(const BspVertex &v) {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

struct CameraBasis {
    Vec3 cameraPosition;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

CameraBasis makeCamera(double centerX, double centerY, double centerZ,
                       double yaw, double pitch, double distance) {
    // Source uses Z-up coordinates. Orbit yaw rotates around Z; pitch is the
    // camera elevation above the XY plane. Build an orthonormal view basis.
    const Vec3 direction{
        std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch)
    };
    const Vec3 forward = direction * -1.0;
    Vec3 right = cross(forward, Vec3{0.0, 0.0, 1.0});
    if (magnitude(right) < 1e-8) right = {0.0, 1.0, 0.0};
    right = normalized(right);
    const Vec3 up = normalized(cross(right, forward));
    const Vec3 center{centerX, centerY, centerZ};
    return {center + direction * distance, right, up, forward};
}

struct CameraPoint {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
};

CameraPoint toCamera(const BspVertex &vertex, const CameraBasis &camera) {
    const Vec3 relative = toVec(vertex) - camera.cameraPosition;
    return {dot(relative, camera.right), dot(relative, camera.up), dot(relative, camera.forward)};
}

struct RenderFace {
    QPolygonF polygon;
    double depth = 0.0;
    double verticality = 0.0;
    double height = 0.0;
};

} // namespace

Bsp3DView::Bsp3DView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(420, 280);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(uiText("Left-drag: orbit · wheel: zoom · double-click: reset view",
                      "ЛКМ + перетаскивание: орбита · колесо: масштаб · двойной щелчок: сброс"));
    setStyleSheet(QStringLiteral("background: #1a1e24;"));
}

void Bsp3DView::setMap(const QVector<BspVertex> &vertices, const QVector<BspPolygon> &polygons) {
    const bool hadMap = !polygons_.isEmpty();
    const double oldCenterX = centerX_;
    const double oldCenterY = centerY_;
    const double oldCenterZ = centerZ_;
    const double oldDiameter = mapDiameter_;

    vertices_ = vertices;
    polygons_ = polygons;

    bool haveBounds = false;
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    minZ_ = std::numeric_limits<double>::max();
    maxZ_ = std::numeric_limits<double>::lowest();

    for (const BspPolygon &polygon : polygons_) {
        for (const BspVertex &vertex : polygon.vertices) {
            const Vec3 point = toVec(vertex);
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                continue;
            if (!haveBounds) {
                minX = maxX = point.x;
                minY = maxY = point.y;
                minZ_ = maxZ_ = point.z;
                haveBounds = true;
            } else {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
                minZ_ = std::min(minZ_, point.z);
                maxZ_ = std::max(maxZ_, point.z);
            }
        }
    }

    if (!haveBounds) {
        centerX_ = centerY_ = centerZ_ = 0.0;
        minZ_ = 0.0;
        maxZ_ = 1.0;
        mapDiameter_ = 1.0;
        dragging_ = false;
        update();
        return;
    }

    centerX_ = (minX + maxX) * 0.5;
    centerY_ = (minY + maxY) * 0.5;
    centerZ_ = (minZ_ + maxZ_) * 0.5;
    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    const double spanZ = maxZ_ - minZ_;
    mapDiameter_ = std::max(1.0, std::sqrt(spanX * spanX + spanY * spanY + spanZ * spanZ));

    // refreshMapViews() is also called when the sky checkbox changes. Don't
    // snap the user's camera back to the default when bounds are unchanged.
    const double boundTolerance = std::max(0.01, oldDiameter * 1e-6);
    const bool sameMapBounds = hadMap &&
        std::abs(centerX_ - oldCenterX) <= boundTolerance &&
        std::abs(centerY_ - oldCenterY) <= boundTolerance &&
        std::abs(centerZ_ - oldCenterZ) <= boundTolerance &&
        std::abs(mapDiameter_ - oldDiameter) <= boundTolerance;
    if (!sameMapBounds) {
        yaw_ = 0.65;
        pitch_ = 0.58;
        zoom_ = 1.0;
    }
    dragging_ = false;
    update();
}

void Bsp3DView::clearMap() {
    vertices_.clear();
    polygons_.clear();
    centerX_ = centerY_ = centerZ_ = 0.0;
    minZ_ = 0.0;
    maxZ_ = 1.0;
    mapDiameter_ = 1.0;
    yaw_ = 0.65;
    pitch_ = 0.58;
    zoom_ = 1.0;
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

    if (polygons_.isEmpty()) {
        painter.setPen(QColor(205, 211, 220));
        painter.drawText(viewport, Qt::AlignCenter | Qt::TextWordWrap,
            uiText("Load a Source BSP v20 file to inspect its 3D face geometry.",
                   "Загрузите Source BSP v20, чтобы посмотреть геометрию в 3D."));
        return;
    }

    // Perspective projection with a genuine camera basis. The camera distance
    // is based on the bounding-sphere diameter, so maps with very large
    // coordinates still fit at zoom=1 and remain in front of the near plane.
    const double cameraDistance = mapDiameter_ * 2.5 / zoom_;
    const CameraBasis camera = makeCamera(centerX_, centerY_, centerZ_, yaw_, pitch_, cameraDistance);
    const double focalLength = std::min(viewport.width(), viewport.height()) * 2.0;
    const double nearPlane = std::max(1e-4, mapDiameter_ * 1e-4);

    std::vector<RenderFace> renderFaces;
    renderFaces.reserve(static_cast<size_t>(polygons_.size()));
    const double zSpan = std::max(1.0, maxZ_ - minZ_);

    for (const BspPolygon &face : polygons_) {
        if (face.vertices.size() < 3) continue;
        RenderFace renderFace;
        renderFace.polygon.reserve(face.vertices.size());
        double depthAverage = 0.0;
        double zAverage = 0.0;
        bool allVerticesVisible = true;

        for (const BspVertex &vertex : face.vertices) {
            const CameraPoint point = toCamera(vertex, camera);
            if (!std::isfinite(point.depth) || point.depth <= nearPlane) {
                allVerticesVisible = false;
                break;
            }
            const double projectedX = viewport.center().x() + point.x * focalLength / point.depth;
            const double projectedY = viewport.center().y() - point.y * focalLength / point.depth;
            if (!std::isfinite(projectedX) || !std::isfinite(projectedY)) {
                allVerticesVisible = false;
                break;
            }
            renderFace.polygon << QPointF(projectedX, projectedY);
            depthAverage += point.depth;
            zAverage += vertex.z;
        }
        if (!allVerticesVisible || renderFace.polygon.size() < 3) continue;

        renderFace.depth = depthAverage / static_cast<double>(face.vertices.size());
        renderFace.height = std::clamp(
            (zAverage / static_cast<double>(face.vertices.size()) - minZ_) / zSpan, 0.0, 1.0);

        const Vec3 a = toVec(face.vertices[0]);
        const Vec3 b = toVec(face.vertices[1]);
        const Vec3 c = toVec(face.vertices[2]);
        const Vec3 normal = cross(b - a, c - a);
        const double normalLength = magnitude(normal);
        renderFace.verticality = normalLength > 1e-12
            ? std::abs(normal.z) / normalLength : 0.0;

        renderFaces.push_back(std::move(renderFace));
    }

    // Painter's algorithm: render the farthest faces first and the nearest last.
    // This is not a z-buffer; complex intersecting BSP surfaces may still overlap.
    std::sort(renderFaces.begin(), renderFaces.end(),
              [](const RenderFace &left, const RenderFace &right) {
                  return left.depth > right.depth;
              });

    for (const RenderFace &face : renderFaces) {
        const double shade = std::clamp(
            62.0 + 76.0 * face.verticality + 22.0 * face.height, 40.0, 170.0);
        const int red = static_cast<int>(shade * 0.76);
        const int green = static_cast<int>(shade * 0.88);
        const int blue = static_cast<int>(shade);
        painter.setPen(QPen(QColor(std::min(255, red + 18),
                                   std::min(255, green + 18),
                                   std::min(255, blue + 18)), 0.6));
        painter.setBrush(QColor(red, green, blue));
        painter.drawPolygon(face.polygon);
    }

    painter.setPen(QColor(211, 216, 224));
    painter.drawText(QRectF(10, 3, width() - 20, 18), Qt::AlignLeft | Qt::AlignVCenter,
        uiText("Orbit 3D · drag to orbit · wheel to zoom · double-click to reset · visible faces: %1",
               "3D-орбита · тяните для вращения · колесо — масштаб · двойной щелчок — сброс · граней: %1")
            .arg(static_cast<int>(renderFaces.size())));
}

void Bsp3DView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !polygons_.isEmpty()) {
        dragging_ = true;
        lastMousePosition_ = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        grabMouse();
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
        if (!delta.isNull()) {
            yaw_ = std::remainder(yaw_ + delta.x() * 0.009, 2.0 * std::numbers::pi);
            pitch_ = std::clamp(pitch_ - delta.y() * 0.009, -1.42, 1.42);
            update();
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void Bsp3DView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        releaseMouse();
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Bsp3DView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !polygons_.isEmpty()) {
        yaw_ = 0.65;
        pitch_ = 0.58;
        zoom_ = 1.0;
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
        update();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void Bsp3DView::wheelEvent(QWheelEvent *event) {
    if (!polygons_.isEmpty()) {
        const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
        zoom_ = std::clamp(zoom_ * std::pow(1.12, steps), 0.25, 2.5);
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

} // namespace bspvision
