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
                       const ScreenVertex &b, const ScreenVertex &c, QRgb color) {
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
                continue;
            }
            depthBuffer[index] = depth;
            pixels[x] = color;
        }
    }
}

} // namespace

Bsp3DView::Bsp3DView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(420, 280);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(uiText(
        "Click: select a target · drag: orbit · wheel: zoom · double-click: reset view",
        "Клик: выбрать цель · перетаскивание: вращение · колесо: масштаб · двойной щелчок: сброс"));
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

    setToolTip(uiText(
        "Click a visible surface to select a target; drag to orbit and use the wheel to zoom.",
        "Кликни по видимой поверхности, чтобы выбрать цель; перетаскивай для вращения, колесо — масштаб."));

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
    hasTarget_ = false;
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

void Bsp3DView::setTarget(double x, double y, double z) {
    targetX_ = x;
    targetY_ = y;
    targetZ_ = z;
    hasTarget_ = true;
    update();
}

void Bsp3DView::clearTarget() {
    hasTarget_ = false;
    update();
}

void Bsp3DView::setObserver(
    double x, double y, double z, double yawDeg, double pitchDeg, bool valid) {
    observerX_ = x;
    observerY_ = y;
    observerZ_ = z;
    observerYawDeg_ = yawDeg;
    observerPitchDeg_ = pitchDeg;
    hasObserver_ = valid;
    update();
}

void Bsp3DView::setTrajectory(const QVector<tfc::Vec3> &points) {
    trajectory_ = points;
    update();
}

void Bsp3DView::setTargetCallback(
    std::function<void(double, double, double)> callback) {
    targetCallback_ = std::move(callback);
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
    for (const BspPolygon &face : polygons_) {
        if (face.vertices.size() < 3) continue;

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

            rasterizeTriangle(
                meshImage, depthBuffer, viewport, a, b, c, faceColor);
        }
    }

    painter.drawImage(QPoint(0, 0), meshImage);

    const auto projectWorld = [&](const tfc::Vec3 &world, QPointF &screen, double &depth) {
        const Vec3 relative{
            world.x - camera.cameraPosition.x,
            world.y - camera.cameraPosition.y,
            world.z - camera.cameraPosition.z
        };
        depth = dot(relative, camera.forward);
        if (!std::isfinite(depth) || depth < nearPlane) return false;
        const double sx = viewport.center().x() +
            dot(relative, camera.right) * focalLength / depth;
        const double sy = viewport.center().y() -
            dot(relative, camera.up) * focalLength / depth;
        if (!std::isfinite(sx) || !std::isfinite(sy)) return false;
        screen = QPointF(sx, sy);
        return true;
    };

    // The predicted shell path is an overlay: keep it visible over the BSP mesh.
    if (trajectory_.size() > 1) {
        painter.setPen(QPen(QColor(225, 166, 83), 2.4, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        for (qsizetype i = 1; i < trajectory_.size(); ++i) {
            QPointF a, b;
            double depthA = 0.0, depthB = 0.0;
            if (projectWorld(trajectory_[i - 1], a, depthA) &&
                projectWorld(trajectory_[i], b, depthB)) {
                painter.drawLine(a, b);
            }
        }
    }

    if (hasObserver_) {
        const tfc::Vec3 observer{observerX_, observerY_, observerZ_};
        QPointF center;
        double depth = 0.0;
        if (projectWorld(observer, center, depth)) {
            const double yaw = observerYawDeg_ * std::numbers::pi / 180.0;
            const double pitch = observerPitchDeg_ * std::numbers::pi / 180.0;
            const Vec3 direction{
                std::cos(pitch) * std::cos(yaw),
                std::cos(pitch) * std::sin(yaw),
                -std::sin(pitch)
            };
            const tfc::Vec3 ahead{
                observerX_ + direction.x * mapDiameter_ * 0.08,
                observerY_ + direction.y * mapDiameter_ * 0.08,
                observerZ_ + direction.z * mapDiameter_ * 0.08
            };
            QPointF tip;
            double tipDepth = 0.0;
            painter.setPen(QPen(QColor(157, 190, 123), 2.6, Qt::SolidLine,
                                Qt::RoundCap, Qt::RoundJoin));
            if (projectWorld(ahead, tip, tipDepth))
                painter.drawLine(center, tip);
            painter.setBrush(QColor(157, 190, 123));
            painter.setPen(QPen(QColor(13, 18, 13), 1.5));
            painter.drawEllipse(center, 6.5, 6.5);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(209, 224, 182), 1.2));
            painter.drawEllipse(center, 10.0, 10.0);
        }
    }

    if (hasTarget_) {
        const Vec3 relative =
            Vec3{targetX_, targetY_, targetZ_} - camera.cameraPosition;
        const double depth = dot(relative, camera.forward);
        if (depth >= nearPlane) {
            const QPointF marker(
                viewport.center().x() +
                    dot(relative, camera.right) * focalLength / depth,
                viewport.center().y() -
                    dot(relative, camera.up) * focalLength / depth);
            const int pixelX = static_cast<int>(std::lround(marker.x()));
            const int pixelY = static_cast<int>(std::lround(marker.y()));
            if (viewport.contains(marker) && pixelX >= 0 && pixelY >= 0 &&
                pixelX < meshImage.width() && pixelY < meshImage.height() &&
                depth <= depthBuffer[static_cast<size_t>(pixelY) *
                                    static_cast<size_t>(meshImage.width()) +
                                    static_cast<size_t>(pixelX)] + 1.0) {
                painter.setPen(QPen(QColor(255, 178, 76), 2));
                painter.drawLine(marker + QPointF(-10, 0), marker + QPointF(10, 0));
                painter.drawLine(marker + QPointF(0, -10), marker + QPointF(0, 10));
                painter.drawEllipse(marker, 5, 5);
            }
        }
    }
    painter.setPen(QColor(226, 232, 241));
    painter.setPen(QColor(226, 232, 241));
    painter.drawText(QRectF(10, 8, width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter,
        uiText("Click a surface to select · drag to orbit · wheel to zoom · double-click to reset",
               "Клик — выбрать цель · перетаскивание — вращать · колесо — масштаб · двойной щелчок — сброс"));
}

bool Bsp3DView::pickTarget(
    const QPointF &screenPosition, double &x, double &y, double &z) const {
    const QRectF viewport = viewportRect();
    if (!viewport.contains(screenPosition) || polygons_.isEmpty())
        return false;

    const double cameraDistance = mapDiameter_ * 2.5 / zoom_;
    const CameraBasis camera =
        makeCamera(centerX_, centerY_, centerZ_, yaw_, pitch_, cameraDistance);
    const double focalLength = std::min(viewport.width(), viewport.height()) * 2.0;
    if (focalLength <= 0.0)
        return false;

    const Vec3 ray = normalized(
        camera.forward +
        camera.right * ((screenPosition.x() - viewport.center().x()) / focalLength) +
        camera.up * ((viewport.center().y() - screenPosition.y()) / focalLength));
    const Vec3 origin = camera.cameraPosition;
    double nearest = std::numeric_limits<double>::infinity();

    for (const BspPolygon &polygon : polygons_) {
        if (polygon.vertices.size() < 3)
            continue;

        const Vec3 first = toVec(polygon.vertices[0]);
        for (qsizetype i = 1; i + 1 < polygon.vertices.size(); ++i) {
            const Vec3 second = toVec(polygon.vertices[i]);
            const Vec3 third = toVec(polygon.vertices[i + 1]);
            const Vec3 edge1 = second - first;
            const Vec3 edge2 = third - first;
            const Vec3 p = cross(ray, edge2);
            const double determinant = dot(edge1, p);
            if (std::abs(determinant) < 1e-9)
                continue;

            const double inverseDeterminant = 1.0 / determinant;
            const Vec3 fromFirst = origin - first;
            const double u = dot(fromFirst, p) * inverseDeterminant;
            if (u < 0.0 || u > 1.0)
                continue;

            const Vec3 q = cross(fromFirst, edge1);
            const double v = dot(ray, q) * inverseDeterminant;
            if (v < 0.0 || u + v > 1.0)
                continue;

            const double distance = dot(edge2, q) * inverseDeterminant;
            if (distance > 0.0 && distance < nearest)
                nearest = distance;
        }
    }

    if (!std::isfinite(nearest))
        return false;

    const Vec3 point = origin + ray * nearest;
    x = point.x;
    y = point.y;
    z = point.z;
    return true;
}

void Bsp3DView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !polygons_.isEmpty()) {
        dragging_ = true;
        pressMousePosition_ = event->position().toPoint();
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
        if ((current - pressMousePosition_).manhattanLength() > 5 &&
            !delta.isNull()) {
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
        if ((event->position().toPoint() - pressMousePosition_).manhattanLength() <= 5) {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (pickTarget(event->position(), x, y, z)) {
                setTarget(x, y, z);
                if (targetCallback_)
                    targetCallback_(x, y, z);
            }
        }
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
