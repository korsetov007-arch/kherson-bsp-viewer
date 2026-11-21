#include "triangle_bvh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace bspvision {
namespace {

using Vec3d = TriangleBvh::Vec3d;

Vec3d toDouble(const BspVertex &v) {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}
Vec3d toDouble(const QVector3D &v) {
    return {static_cast<double>(v.x()), static_cast<double>(v.y()), static_cast<double>(v.z())};
}
Vec3d add(const Vec3d &a, const Vec3d &b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3d subtract(const Vec3d &a, const Vec3d &b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3d multiply(const Vec3d &a, double s) { return {a.x*s, a.y*s, a.z*s}; }
double dot(const Vec3d &a, const Vec3d &b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3d cross(const Vec3d &a, const Vec3d &b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
double lengthSquared(const Vec3d &v) { return dot(v, v); }
bool finite(const Vec3d &v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
Vec3d componentMin(const Vec3d &a, const Vec3d &b) {
    return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};
}
Vec3d componentMax(const Vec3d &a, const Vec3d &b) {
    return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};
}
QVector3D toFloat(const Vec3d &v) {
    return QVector3D(static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z));
}
double component(const Vec3d &v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

} // namespace

void TriangleBvh::clear() {
    triangles_.clear();
    primitiveOrder_.clear();
    nodes_.clear();
}

bool TriangleBvh::build(const QVector<BspPolygon> &polygons, QString *error) {
    clear();
    if (error) error->clear();

    for (const BspPolygon &polygon : polygons) {
        if (polygon.skySurface || polygon.vertices.size() < 3) continue;
        const Vec3d root = toDouble(polygon.vertices[0]);
        if (!finite(root)) continue;

        for (qsizetype i = 1; i + 1 < polygon.vertices.size(); ++i) {
            const Vec3d b = toDouble(polygon.vertices[i]);
            const Vec3d c = toDouble(polygon.vertices[i + 1]);
            if (!finite(b) || !finite(c)) continue;
            const Vec3d normalRaw = cross(subtract(b, root), subtract(c, root));
            const double normalLength2 = lengthSquared(normalRaw);
            if (!std::isfinite(normalLength2) || normalLength2 <= 1e-12) continue;

            const double invNormalLength = 1.0 / std::sqrt(normalLength2);
            Triangle triangle;
            triangle.a = root;
            triangle.b = b;
            triangle.c = c;
            triangle.boundsMin = componentMin(root, componentMin(b, c));
            triangle.boundsMax = componentMax(root, componentMax(b, c));
            triangle.centroid = multiply(add(add(root, b), c), 1.0 / 3.0);
            triangle.normal = multiply(normalRaw, invNormalLength);
            triangle.materialName = polygon.materialName;
            triangles_.push_back(std::move(triangle));
        }
    }

    if (triangles_.isEmpty()) {
        clear();
        if (error) *error = QStringLiteral("No non-degenerate BSP surface triangles were available for the BVH.");
        return false;
    }

    primitiveOrder_.resize(triangles_.size());
    std::iota(primitiveOrder_.begin(), primitiveOrder_.end(), 0);
    nodes_.reserve(triangles_.size() / 4 + 1);
    buildNode(0, static_cast<int>(primitiveOrder_.size()));

    if (error) error->clear();
    return isValid();
}

int TriangleBvh::buildNode(int first, int last) {
    Node node;
    node.boundsMin = {std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max()};
    node.boundsMax = {-std::numeric_limits<double>::max(),
                      -std::numeric_limits<double>::max(),
                      -std::numeric_limits<double>::max()};
    Vec3d centroidMin = node.boundsMin;
    Vec3d centroidMax = node.boundsMax;

    for (int orderIndex = first; orderIndex < last; ++orderIndex) {
        const Triangle &triangle = triangles_[primitiveOrder_[orderIndex]];
        node.boundsMin = componentMin(node.boundsMin, triangle.boundsMin);
        node.boundsMax = componentMax(node.boundsMax, triangle.boundsMax);
        centroidMin = componentMin(centroidMin, triangle.centroid);
        centroidMax = componentMax(centroidMax, triangle.centroid);
    }

    const int nodeIndex = static_cast<int>(nodes_.size());
    nodes_.push_back(node);
    const int count = last - first;
    constexpr int kLeafTriangleCount = 8;
    if (count <= kLeafTriangleCount) {
        nodes_[nodeIndex].first = first;
        nodes_[nodeIndex].count = count;
        return nodeIndex;
    }

    const Vec3d centroidSpan = subtract(centroidMax, centroidMin);
    int axis = 0;
    if (centroidSpan.y > centroidSpan.x) axis = 1;
    if (component(centroidSpan, 2) > component(centroidSpan, axis)) axis = 2;
    const int middle = first + count / 2;
    std::nth_element(primitiveOrder_.begin() + first,
                     primitiveOrder_.begin() + middle,
                     primitiveOrder_.begin() + last,
        [this, axis](int lhs, int rhs) {
            return component(triangles_[lhs].centroid, axis) <
                   component(triangles_[rhs].centroid, axis);
        });

    const int left = buildNode(first, middle);
    const int right = buildNode(middle, last);
    nodes_[nodeIndex].left = left;
    nodes_[nodeIndex].right = right;
    return nodeIndex;
}

bool TriangleBvh::intersectNodeBounds(const Node &node, const Vec3d &start,
                                      const Vec3d &direction, double maxFraction) const {
    double lower = 0.0;
    double upper = maxFraction;
    constexpr double kParallelEpsilon = 1e-12;
    for (int axis = 0; axis < 3; ++axis) {
        const double origin = component(start, axis);
        const double delta = component(direction, axis);
        const double minimum = component(node.boundsMin, axis) - 1e-5;
        const double maximum = component(node.boundsMax, axis) + 1e-5;
        if (std::abs(delta) < kParallelEpsilon) {
            if (origin < minimum || origin > maximum) return false;
            continue;
        }
        const double inv = 1.0 / delta;
        double t0 = (minimum - origin) * inv;
        double t1 = (maximum - origin) * inv;
        if (t0 > t1) std::swap(t0, t1);
        lower = std::max(lower, t0);
        upper = std::min(upper, t1);
        if (lower > upper) return false;
    }
    return upper >= 0.0 && lower <= maxFraction;
}

bool TriangleBvh::intersectTriangle(const Triangle &triangle, const Vec3d &start,
                                    const Vec3d &direction, double *fraction) const {
    const Vec3d edge1 = subtract(triangle.b, triangle.a);
    const Vec3d edge2 = subtract(triangle.c, triangle.a);
    const Vec3d p = cross(direction, edge2);
    const double determinant = dot(edge1, p);
    constexpr double kDeterminantEpsilon = 1e-10;
    constexpr double kBarycentricEpsilon = 1e-8;
    if (std::abs(determinant) < kDeterminantEpsilon) return false;

    const double invDeterminant = 1.0 / determinant;
    const Vec3d tVec = subtract(start, triangle.a);
    const double u = dot(tVec, p) * invDeterminant;
    if (u < -kBarycentricEpsilon || u > 1.0 + kBarycentricEpsilon) return false;

    const Vec3d q = cross(tVec, edge1);
    const double v = dot(direction, q) * invDeterminant;
    if (v < -kBarycentricEpsilon || u + v > 1.0 + kBarycentricEpsilon) return false;

    const double t = dot(edge2, q) * invDeterminant;
    // Ignore a hit exactly at the segment origin. Launch points often sit on a
    // floor surface; treating t=0 as a new collision would truncate every arc.
    if (t <= 1e-5 || t > 1.0 + kBarycentricEpsilon || !std::isfinite(t))
        return false;

    if (fraction) *fraction = std::clamp(t, 0.0, 1.0);
    return true;
}

bool TriangleBvh::intersectSegment(const QVector3D &startVector, const QVector3D &endVector,
                                   SegmentHit *hit) const {
    if (!isValid()) return false;
    const Vec3d start = toDouble(startVector);
    const Vec3d end = toDouble(endVector);
    const Vec3d direction = subtract(end, start);
    if (!finite(start) || !finite(end) || lengthSquared(direction) <= 1e-16) return false;

    double bestFraction = 1.0 + 1e-9;
    int bestTriangle = -1;
    QVector<int> stack;
    stack.reserve(64);
    stack.push_back(0);

    while (!stack.isEmpty()) {
        const int nodeIndex = stack.takeLast();
        const Node &node = nodes_[nodeIndex];
        if (!intersectNodeBounds(node, start, direction, bestFraction)) continue;

        if (node.count > 0) {
            for (int orderIndex = node.first; orderIndex < node.first + node.count; ++orderIndex) {
                const int triangleIndex = primitiveOrder_[orderIndex];
                double fraction = 1.0;
                if (intersectTriangle(triangles_[triangleIndex], start, direction, &fraction) &&
                    fraction < bestFraction) {
                    bestFraction = fraction;
                    bestTriangle = triangleIndex;
                }
            }
        } else {
            if (node.left >= 0) stack.push_back(node.left);
            if (node.right >= 0) stack.push_back(node.right);
        }
    }

    if (bestTriangle < 0) return false;
    if (hit) {
        const Triangle &triangle = triangles_[bestTriangle];
        hit->fraction = bestFraction;
        hit->position = toFloat(add(start, multiply(direction, bestFraction)));
        hit->normal = toFloat(triangle.normal);
        hit->triangleIndex = bestTriangle;
        hit->materialName = triangle.materialName;
    }
    return true;
}

} // namespace bspvision
