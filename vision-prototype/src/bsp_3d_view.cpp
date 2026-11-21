#include "bsp_3d_view.hpp"
#include "app_language.hpp"

#include <QImage>
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

struct CameraVertex {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
    double worldZ = 0.0;
};

struct ScreenVertex {
    double x = 0.0;
    double y = 0.0;
    double inverseDepth = 0.0;
};

double edgeFunction(const ScreenVertex &a, const ScreenVertex &b,
                    double x, double y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

// A small software depth buffer keeps overlapping BSP faces from incorrectly
// hiding one another just because their average depths happen to sort badly.
// Interpolating inverse depth is perspective-correct for screen-space triangles.
void rasterizeTriangle(QImage &image, std::vector<double> &depthBuffer,
                       const QRectF &viewport, const ScreenVertex &a,
                       const ScreenVertex &b, const ScreenVertex &c, QRgb color,
                       quint64 *pixelsWritten, quint64 *fragmentsOccluded) {
    const double area = edgeFunction(a, b, c.x, c.y);
    if (!std::isfinite(area) || std::abs(area) < 1e-8) return;

    const int imageWidth = image.width();
    const int imageHeight = image.height();
    const int minX = std::max(0, static_cast<int>(std::floor(
        std::max(viewport.left(), std::min({a.x, b.x, c.x})))));
    const int maxX = std::min(imageWidth - 1, static_cast<int>(std::ceil(
        std::min(viewport.right(), std::max({a.x, b.x, c.x})))));
    const int minY = std::max(0, static_cast<int>(std::floor(
        std::max(viewport.top(), std::min({a.y, b.y, c.y})))));
    const int maxY = std::min(imageHeight - 1, static_cast<int>(std::ceil(
        std::min(viewport.bottom(), std::max({a.y, b.y, c.y})))));

    if (minX > maxX || minY > maxY) return;

    constexpr double edgeTolerance = -1e-7;
    for (int y = minY; y <= maxY; ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        const double sampleY = static_cast<double>(y) + 0.5;
        for (int x = minX; x <= maxX; ++x) {
            const double sampleX = static_cast<double>(x) + 0.5;
            const double w0 = edgeFunction(b, c, sampleX, sampleY) / area;
            const double w1 = edgeFunction(c, a, sampleX, sampleY) / area;
            const double w2 = edgeFunction(a, b, sampleX, sampleY) / area;
            if (w0 < edgeTolerance || w1 < edgeTolerance || w2 < edgeTolerance)
                continue;

            const double inverseDepth =
                w0 * a.inverseDepth + w1 * b.inverseDepth + w2 * c.inverseDepth;
            if (!std::isfinite(inverseDepth) || inverseDepth <= 0.0) continue;
            const double depth = 1.0 / inverseDepth;
            const size_t index = static_cast<size_t>(y) *
                                 static_cast<size_t>(imageWidth) +
                                 static_cast<size_t>(x);
            if (depth >= depthBuffer[index]) {
                if (fragmentsOccluded) ++(*fragmentsOccluded);
                continue;
            }
            depthBuffer[index] = depth;
            pixels[x] = color;
            if (pixelsWritten) ++(*pixelsWritten);
        }
    }
}

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

    bool haveFaceBounds = false;
    bool haveDisplacementBounds = false;
    double faceMinX = 0.0, faceMaxX = 0.0, faceMinY = 0.0, faceMaxY = 0.0;
    double faceMinZ = 0.0, faceMaxZ = 0.0;
    double dispMinX = 0.0, dispMaxX = 0.0, dispMinY = 0.0, dispMaxY = 0.0;
    double dispMinZ = 0.0, dispMaxZ = 0.0;

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

            bool &haveTypeBounds = polygon.displacementSurface
                ? haveDisplacementBounds : haveFaceBounds;
            double &typeMinX = polygon.displacementSurface ? dispMinX : faceMinX;
            double &typeMaxX = polygon.displacementSurface ? dispMaxX : faceMaxX;
            double &typeMinY = polygon.displacementSurface ? dispMinY : faceMinY;
            double &typeMaxY = polygon.displacementSurface ? dispMaxY : faceMaxY;
            double &typeMinZ = polygon.displacementSurface ? dispMinZ : faceMinZ;
            double &typeMaxZ = polygon.displacementSurface ? dispMaxZ : faceMaxZ;
            if (!haveTypeBounds) {
                typeMinX = typeMaxX = point.x;
                typeMinY = typeMaxY = point.y;
                typeMinZ = typeMaxZ = point.z;
                haveTypeBounds = true;
            } else {
                typeMinX = std::min(typeMinX, point.x);
                typeMaxX = std::max(typeMaxX, point.x);
                typeMinY = std::min(typeMinY, point.y);
                typeMaxY = std::max(typeMaxY, point.y);
                typeMinZ = std::min(typeMinZ, point.z);
                typeMaxZ = std::max(typeMaxZ, point.z);
            }
        }
    }

    const QString faceBoundsText = haveFaceBounds
        ? QStringLiteral("X[%1,%2] Y[%3,%4] Z[%5,%6]")
              .arg(faceMinX, 0, 'f', 0).arg(faceMaxX, 0, 'f', 0)
              .arg(faceMinY, 0, 'f', 0).arg(faceMaxY, 0, 'f', 0)
              .arg(faceMinZ, 0, 'f', 0).arg(faceMaxZ, 0, 'f', 0)
        : QStringLiteral("none");
    const QString displacementBoundsText = haveDisplacementBounds
        ? QStringLiteral("X[%1,%2] Y[%3,%4] Z[%5,%6]")
              .arg(dispMinX, 0, 'f', 0).arg(dispMaxX, 0, 'f', 0)
              .arg(dispMinY, 0, 'f', 0).arg(dispMaxY, 0, 'f', 0)
              .arg(dispMinZ, 0, 'f', 0).arg(dispMaxZ, 0, 'f', 0)
        : QStringLiteral("none");
    setToolTip(uiText(
        "BSP face bounds: %1\nDisplacement bounds: %2",
        "Границы BSP faces: %1\nГраницы displacement: %2")
        .arg(faceBoundsText, displacementBoundsText));

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
    return QRectF(rect()).adjusted(18.0, 40.0, -18.0, -18.0);
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

    const int imageWidth = std::max(1, width());
    const int imageHeight = std::max(1, height());
    QImage meshImage(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    meshImage.fill(Qt::transparent);
    std::vector<double> depthBuffer(
        static_cast<size_t>(imageWidth) * static_cast<size_t>(imageHeight),
        std::numeric_limits<double>::infinity());

    // The camera is kept far enough from the map's bounding sphere to keep the
    // complete map in view at the default zoom. Zoom changes only camera distance.
    const double cameraDistance = mapDiameter_ * 2.5 / zoom_;
    const CameraBasis camera = makeCamera(
        centerX_, centerY_, centerZ_, yaw_, pitch_, cameraDistance);
    const double focalLength = std::min(viewport.width(), viewport.height()) * 2.0;
    const double nearPlane = std::max(1e-4, mapDiameter_ * 1e-5);
    const double zSpan = std::max(1.0, maxZ_ - minZ_);
    const Vec3 lightDirection = normalized(Vec3{-0.42, -0.56, 0.72});
    int sourceFacePolygons = 0;
    int displacementPolygons = 0;
    int sourceProjectedTriangles = 0;
    int displacementProjectedTriangles = 0;
    int sourceRasterizedTriangles = 0;
    int displacementRasterizedTriangles = 0;
    quint64 sourcePixelsWritten = 0;
    quint64 displacementPixelsWritten = 0;
    quint64 sourceFragmentsOccluded = 0;
    quint64 displacementFragmentsOccluded = 0;

    for (const BspPolygon &face : polygons_) {
        if (face.vertices.size() < 3) continue;
        if (face.displacementSurface) ++displacementPolygons;
        else ++sourceFacePolygons;

        // Find a stable face normal; the first three points can be collinear on
        // some imported faces even though a later triple defines the plane.
        Vec3 normal{};
        double normalLength = 0.0;
        const Vec3 origin = toVec(face.vertices[0]);
        for (qsizetype i = 1; i + 1 < face.vertices.size(); ++i) {
            normal = cross(toVec(face.vertices[i]) - origin,
                           toVec(face.vertices[i + 1]) - origin);
            normalLength = magnitude(normal);
            if (normalLength > 1e-12 && std::isfinite(normalLength)) break;
        }
        if (normalLength <= 1e-12 || !std::isfinite(normalLength)) continue;
        const Vec3 unitNormal = normal * (1.0 / normalLength);
        const double illumination = 0.22 + 0.78 * std::abs(dot(unitNormal, lightDirection));

        double zAverage = 0.0;
        int finiteVertexCount = 0;
        std::vector<CameraVertex> clipped;
        clipped.reserve(static_cast<size_t>(face.vertices.size()) + 2U);

        for (const BspVertex &vertex : face.vertices) {
            const CameraPoint point = toCamera(vertex, camera);
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(point.depth) || !std::isfinite(vertex.z))
                continue;
            clipped.push_back({point.x, point.y, point.depth,
                               static_cast<double>(vertex.z)});
            zAverage += vertex.z;
            ++finiteVertexCount;
        }
        if (finiteVertexCount < 3 || clipped.size() < 3) continue;
        zAverage /= static_cast<double>(finiteVertexCount);
        const double normalizedHeight = std::clamp(
            (zAverage - minZ_) / zSpan, 0.0, 1.0);
        const double shade = std::clamp(
            58.0 + 112.0 * illumination + 18.0 * normalizedHeight, 58.0, 190.0);
        // Use different restrained tints for world brushes/faces and displacement
        // terrain so walls and buildings do not disappear into the dark background.
        const int red = static_cast<int>(shade * (face.displacementSurface ? 0.88 : 0.70));
        const int green = static_cast<int>(shade * (face.displacementSurface ? 0.94 : 0.84));
        const int blue = static_cast<int>(shade * (face.displacementSurface ? 0.82 : 1.00));
        const QRgb faceColor = qRgba(red, green, blue, 255);

        // Clip each polygon against the near plane before projection. Rejecting
        // a whole face because one corner crosses the plane caused visible holes.
        std::vector<CameraVertex> nearClipped;
        nearClipped.reserve(clipped.size() + 2U);
        CameraVertex previous = clipped.back();
        bool previousInside = previous.depth >= nearPlane;
        for (const CameraVertex &current : clipped) {
            const bool currentInside = current.depth >= nearPlane;
            if (currentInside != previousInside) {
                const double denominator = current.depth - previous.depth;
                if (std::abs(denominator) > 1e-15) {
                    const double t = std::clamp(
                        (nearPlane - previous.depth) / denominator, 0.0, 1.0);
                    nearClipped.push_back({
                        previous.x + (current.x - previous.x) * t,
                        previous.y + (current.y - previous.y) * t,
                        nearPlane,
                        previous.worldZ + (current.worldZ - previous.worldZ) * t
                    });
                }
            }
            if (currentInside) nearClipped.push_back(current);
            previous = current;
            previousInside = currentInside;
        }
        if (nearClipped.size() < 3) continue;

        std::vector<ScreenVertex> projected;
        projected.reserve(nearClipped.size());
        bool validProjection = true;
        for (const CameraVertex &point : nearClipped) {
            const double inverseDepth = 1.0 / point.depth;
            const double projectedX = viewport.center().x() +
                                      point.x * focalLength * inverseDepth;
            const double projectedY = viewport.center().y() -
                                      point.y * focalLength * inverseDepth;
            if (!std::isfinite(projectedX) || !std::isfinite(projectedY) ||
                !std::isfinite(inverseDepth)) {
                validProjection = false;
                break;
            }
            projected.push_back({projectedX, projectedY, inverseDepth});
        }
        if (!validProjection || projected.size() < 3) continue;

        // BSP faces are convex. A fan triangulation plus a per-pixel depth buffer
        // handles occlusion consistently for floors, walls, ceilings and terrain.
        for (size_t i = 1; i + 1 < projected.size(); ++i) {
            const ScreenVertex &a = projected[0];
            const ScreenVertex &b = projected[i];
            const ScreenVertex &c = projected[i + 1];
            const double area = std::abs(edgeFunction(a, b, c.x, c.y));
            if (!std::isfinite(area) || area < 1e-7) continue;

            const double triangleMinX = std::min({a.x, b.x, c.x});
            const double triangleMaxX = std::max({a.x, b.x, c.x});
            const double triangleMinY = std::min({a.y, b.y, c.y});
            const double triangleMaxY = std::max({a.y, b.y, c.y});
            if (triangleMaxX < viewport.left() || triangleMinX > viewport.right() ||
                triangleMaxY < viewport.top() || triangleMinY > viewport.bottom())
                continue;

            quint64 &pixelCounter = face.displacementSurface
                ? displacementPixelsWritten : sourcePixelsWritten;
            quint64 &occludedCounter = face.displacementSurface
                ? displacementFragmentsOccluded : sourceFragmentsOccluded;
            const quint64 pixelsBefore = pixelCounter;
            rasterizeTriangle(meshImage, depthBuffer, viewport, a, b, c,
                              faceColor, &pixelCounter, &occludedCounter);
            if (face.displacementSurface) {
                ++displacementProjectedTriangles;
                if (pixelCounter > pixelsBefore) ++displacementRasterizedTriangles;
            } else {
                ++sourceProjectedTriangles;
                if (pixelCounter > pixelsBefore) ++sourceRasterizedTriangles;
            }
        }
    }

    painter.drawImage(QPoint(0, 0), meshImage);
    painter.setPen(QColor(226, 232, 241));
    painter.drawText(QRectF(10, 2, width() - 20, 17), Qt::AlignLeft | Qt::AlignVCenter,
        uiText("Orbit 3D · drag: orbit · wheel: zoom (up to 24x) · double-click: reset",
               "3D-орбита · ЛКМ: вращение · колесо: зум до 24× · двойной щелчок: сброс"));
    painter.setPen(QColor(175, 198, 218));
    painter.drawText(QRectF(10, 20, width() - 20, 17), Qt::AlignLeft | Qt::AlignVCenter,
        uiText("BSP faces: %1 (%2 rasterized, %3 occluded) · displacement: %4 (%5 rasterized, %6 occluded)",
               "Грани BSP: %1 (%2 отрисовано, %3 скрыто) · displacement: %4 (%5 отрисовано, %6 скрыто)")
            .arg(sourceFacePolygons)
            .arg(sourceRasterizedTriangles)
            .arg(sourceFragmentsOccluded)
            .arg(displacementPolygons)
            .arg(displacementRasterizedTriangles)
            .arg(displacementFragmentsOccluded));
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
        zoom_ = std::clamp(zoom_ * std::pow(1.14, steps), 0.15, 24.0);
        update();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

} // namespace bspvision
