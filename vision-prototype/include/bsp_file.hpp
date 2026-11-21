#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>
#include <cstdint>

namespace bspvision {

struct BspVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct BspEdge {
    quint16 first = 0;
    quint16 second = 0;
};

struct BspFace {
    qint32 firstEdge = 0;
    qint16 edgeCount = 0;
    qint16 textureInfo = -1;
    qint16 displacementInfo = -1;
};

struct BspPolygon {
    QVector<BspVertex> vertices;
};

struct BspLumpInfo {
    QString name;
    qint32 offset = 0;
    qint32 length = 0;
    qint32 version = 0;
    quint32 fourCC = 0;
    qint32 recordSize = 0;
    qint32 recordCount = -1;
};

class BspFile final {
public:
    static constexpr qint32 SupportedVersion = 20;
    static constexpr qint32 HeaderSize = 1036;
    static constexpr int LumpCount = 64;

    bool load(const QString &path, QString *error = nullptr);
    void clear();

    bool isLoaded() const { return loaded_; }
    QString filePath() const { return filePath_; }
    qint64 fileSize() const { return bytes_.size(); }
    qint32 version() const { return version_; }

    const QVector<BspVertex> &vertices() const { return vertices_; }
    const QVector<BspEdge> &edges() const { return edges_; }
    const QVector<qint32> &surfEdges() const { return surfEdges_; }
    const QVector<BspFace> &faces() const { return faces_; }
    const std::array<BspLumpInfo, LumpCount> &lumps() const { return lumps_; }

    QVector<BspPolygon> facePolygons() const;
    QString summary() const;

private:
    QByteArray bytes_;
    QString filePath_;
    qint32 version_ = 0;
    bool loaded_ = false;
    std::array<BspLumpInfo, LumpCount> lumps_{};
    QVector<BspVertex> vertices_;
    QVector<BspEdge> edges_;
    QVector<qint32> surfEdges_;
    QVector<BspFace> faces_;
};

} // namespace bspvision
