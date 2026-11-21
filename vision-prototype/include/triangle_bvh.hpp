#pragma once

#include "bsp_file.hpp"

#include <QVector3D>
#include <QString>
#include <QVector>

namespace bspvision {

struct SegmentHit {
    QVector3D position;
    QVector3D normal;
    double fraction = 1.0; // 0 at segment start, 1 at segment end
    int triangleIndex = -1;
    QString materialName;
};

class TriangleBvh final {
public:
    // Internal double-precision vector exposed for translation-unit helper routines.
    struct Vec3d {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    bool build(const QVector<BspPolygon> &polygons, QString *error = nullptr);
    void clear();

    bool isValid() const { return !triangles_.isEmpty() && !nodes_.isEmpty(); }
    int triangleCount() const { return triangles_.size(); }

    // Tests a finite segment, not an infinite ray. Sky surfaces are excluded at build time.
    bool intersectSegment(const QVector3D &start, const QVector3D &end,
                          SegmentHit *hit = nullptr) const;

private:
    struct Triangle {
        Vec3d a, b, c;
        Vec3d boundsMin, boundsMax, centroid, normal;
        QString materialName;
    };
    struct Node {
        Vec3d boundsMin, boundsMax;
        int left = -1;
        int right = -1;
        int first = 0;
        int count = 0;
    };

    int buildNode(int first, int last);
    bool intersectNodeBounds(const Node &node, const Vec3d &start,
                             const Vec3d &direction, double maxFraction) const;
    bool intersectTriangle(const Triangle &triangle, const Vec3d &start,
                           const Vec3d &direction, double *fraction) const;

    QVector<Triangle> triangles_;
    QVector<int> primitiveOrder_;
    QVector<Node> nodes_;
};

} // namespace bspvision
