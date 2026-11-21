#include "bsp_file.hpp"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace bspvision {
namespace {

struct LumpDefinition {
    const char *name;
    qint32 recordSize;
};

constexpr std::array<LumpDefinition, BspFile::LumpCount> kLumpDefinitions{{
    {"Entities", 1}, {"Planes", 20}, {"Texture data", 32}, {"Vertices", 12},
    {"Visibility", 1}, {"Nodes", 32}, {"Texture info", 72}, {"Faces", 56},
    {"Lighting", 1}, {"Occlusion", 1}, {"Leaves", 32}, {"Face IDs", 2},
    {"Edges", 4}, {"Surf edges", 4}, {"Models", 48}, {"World lights", 88},
    {"Leaf faces", 2}, {"Leaf brushes", 2}, {"Brushes", 12}, {"Brush sides", 8},
    {"Areas", 8}, {"Area portals", 12}, {"Portals", 1}, {"Clusters", 1},
    {"Portal vertices", 1}, {"Cluster portals", 1}, {"Displacement info", 176},
    {"Original faces", 56}, {"Physics displacement", 1}, {"Physics collision", 1},
    {"Vertex normals", 12}, {"Vertex normal indices", 2}, {"Displacement lightmap alphas", 1},
    {"Displacement vertices", 20}, {"Displacement lightmap samples", 1},
    {"Game lump", 1}, {"Leaf water data", 4}, {"Primitives", 10},
    {"Primitive vertices", 1}, {"Primitive indices", 2}, {"Packed files", 1},
    {"Clip portal vertices", 1}, {"Cubemaps", 16}, {"Texture string data", 1},
    {"Texture string table", 4}, {"Overlays", 352}, {"Leaf min distance to water", 4},
    {"Face macro texture info", 1}, {"Displacement triangles", 2},
    {"Physics collision surface", 1}, {"Water overlays", 1},
    {"Leaf ambient index HDR", 4}, {"Leaf ambient index", 4},
    {"Lighting HDR", 1}, {"World lights HDR", 88}, {"Leaf ambient lighting HDR", 1},
    {"Leaf ambient lighting", 1}, {"XZIP packed file", 1}, {"Faces HDR", 56},
    {"Map flags", 4}, {"Overlay fades", 1}, {"Overlay system levels", 1},
    {"Physics levels", 1}, {"Displacement multiblend", 1}
}};

quint16 readU16(const uchar *data) {
    return qFromLittleEndian<quint16>(data);
}

qint16 readI16(const uchar *data) {
    return qFromLittleEndian<qint16>(data);
}

qint32 readI32(const uchar *data) {
    return qFromLittleEndian<qint32>(data);
}

quint32 readU32(const uchar *data) {
    return qFromLittleEndian<quint32>(data);
}

float readF32(const uchar *data) {
    return std::bit_cast<float>(readU32(data));
}

QString fail(const QString &message, QString *error) {
    if (error) *error = message;
    return message;
}

} // namespace

void BspFile::clear() {
    bytes_.clear();
    filePath_.clear();
    version_ = 0;
    loaded_ = false;
    vertices_.clear();
    edges_.clear();
    surfEdges_.clear();
    faces_.clear();
    for (int i = 0; i < LumpCount; ++i) lumps_[static_cast<size_t>(i)] = {};
}

bool BspFile::load(const QString &path, QString *error) {
    clear();
    if (error) error->clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open file: ") + file.errorString();
        return false;
    }
    bytes_ = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error) *error = QStringLiteral("Failed while reading BSP: ") + file.errorString();
        clear();
        return false;
    }
    if (bytes_.size() < HeaderSize) {
        if (error) *error = QStringLiteral("File is smaller than the Source BSP header.");
        clear();
        return false;
    }

    const auto *data = reinterpret_cast<const uchar *>(bytes_.constData());
    // Bytes "VBSP" have this value when interpreted as a little-endian uint32.
    if (readU32(data) != 0x50534256U) {
        if (error) *error = QStringLiteral("Invalid BSP signature. Expected VBSP.");
        clear();
        return false;
    }

    version_ = readI32(data + 4);
    if (version_ != SupportedVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported BSP version %1; this prototype currently supports version %2.")
                         .arg(version_).arg(SupportedVersion);
        }
        clear();
        return false;
    }

    const qint64 fileLength = bytes_.size();
    for (int i = 0; i < LumpCount; ++i) {
        const qint64 headerOffset = 8 + static_cast<qint64>(i) * 16;
        auto &lump = lumps_[static_cast<size_t>(i)];
        lump.name = QString::fromLatin1(kLumpDefinitions[static_cast<size_t>(i)].name);
        lump.recordSize = kLumpDefinitions[static_cast<size_t>(i)].recordSize;
        lump.offset = readI32(data + headerOffset);
        lump.length = readI32(data + headerOffset + 4);
        lump.version = readI32(data + headerOffset + 8);
        lump.fourCC = readU32(data + headerOffset + 12);

        if (lump.length < 0 || lump.offset < 0) {
            if (error) *error = QStringLiteral("Lump %1 has a negative offset or length.").arg(lump.name);
            clear();
            return false;
        }
        if (lump.length == 0) {
            lump.recordCount = 0;
            continue;
        }
        const qint64 start = lump.offset;
        const qint64 end = start + static_cast<qint64>(lump.length);
        if (start < HeaderSize || end < start || end > fileLength) {
            if (error) *error = QStringLiteral("Lump %1 points outside the file.").arg(lump.name);
            clear();
            return false;
        }
        if (lump.recordSize > 1 && (lump.length % lump.recordSize) == 0)
            lump.recordCount = lump.length / lump.recordSize;
    }

    const auto &vertexLump = lumps_[3];
    const auto &edgeLump = lumps_[12];
    const auto &surfEdgeLump = lumps_[13];
    const auto &faceLump = lumps_[7];
    if ((vertexLump.length % 12) != 0 || (edgeLump.length % 4) != 0 ||
        (surfEdgeLump.length % 4) != 0 || (faceLump.length % 56) != 0) {
        if (error) *error = QStringLiteral("Core geometry lumps have unexpected record sizes.");
        clear();
        return false;
    }

    vertices_.reserve(vertexLump.length / 12);
    for (qint32 offset = vertexLump.offset; offset < vertexLump.offset + vertexLump.length; offset += 12) {
        const float x = readF32(data + offset);
        const float y = readF32(data + offset + 4);
        const float z = readF32(data + offset + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            if (error) *error = QStringLiteral("Vertex lump contains a non-finite coordinate.");
            clear();
            return false;
        }
        vertices_.push_back({x, y, z});
    }

    edges_.reserve(edgeLump.length / 4);
    for (qint32 offset = edgeLump.offset; offset < edgeLump.offset + edgeLump.length; offset += 4)
        edges_.push_back({readU16(data + offset), readU16(data + offset + 2)});

    surfEdges_.reserve(surfEdgeLump.length / 4);
    for (qint32 offset = surfEdgeLump.offset; offset < surfEdgeLump.offset + surfEdgeLump.length; offset += 4)
        surfEdges_.push_back(readI32(data + offset));

    faces_.reserve(faceLump.length / 56);
    for (qint32 offset = faceLump.offset; offset < faceLump.offset + faceLump.length; offset += 56) {
        BspFace face;
        face.firstEdge = readI32(data + offset + 4);
        face.edgeCount = readI16(data + offset + 8);
        face.textureInfo = readI16(data + offset + 10);
        face.displacementInfo = readI16(data + offset + 12);
        faces_.push_back(face);
    }

    filePath_ = path;
    loaded_ = true;
    return true;
}

QVector<BspPolygon> BspFile::facePolygons() const {
    QVector<BspPolygon> result;
    if (!loaded_) return result;
    result.reserve(faces_.size());

    for (const BspFace &face : faces_) {
        if (face.edgeCount < 3 || face.edgeCount > 4096 || face.firstEdge < 0)
            continue;
        const qint64 edgeEnd = static_cast<qint64>(face.firstEdge) + face.edgeCount;
        if (edgeEnd > surfEdges_.size()) continue;

        BspPolygon polygon;
        polygon.vertices.reserve(face.edgeCount);
        bool valid = true;
        for (qint32 i = 0; i < face.edgeCount; ++i) {
            const qint32 signedEdge = surfEdges_[face.firstEdge + i];
            const qint64 edgeIndex = signedEdge < 0 ? -static_cast<qint64>(signedEdge) : signedEdge;
            if (edgeIndex < 0 || edgeIndex >= edges_.size()) {
                valid = false;
                break;
            }
            const BspEdge &edge = edges_[static_cast<qsizetype>(edgeIndex)];
            const quint16 vertexIndex = signedEdge < 0 ? edge.second : edge.first;
            if (vertexIndex >= vertices_.size()) {
                valid = false;
                break;
            }
            polygon.vertices.push_back(vertices_[vertexIndex]);
        }
        if (valid && polygon.vertices.size() >= 3) result.push_back(std::move(polygon));
    }
    return result;
}

QString BspFile::summary() const {
    if (!loaded_) return QStringLiteral("No BSP loaded.");
    const int validPolygons = facePolygons().size();
    return QStringLiteral(
        "File: %1\n"
        "Format: Source BSP v%2 | Size: %3 MiB\n"
        "Vertices: %4 | Edges: %5 | Surf edges: %6\n"
        "Faces in lump: %7 | Valid polygon outlines: %8")
        .arg(filePath_)
        .arg(version_)
        .arg(static_cast<double>(fileSize()) / (1024.0 * 1024.0), 0, 'f', 2)
        .arg(vertices_.size())
        .arg(edges_.size())
        .arg(surfEdges_.size())
        .arg(faces_.size())
        .arg(validPolygons);
}

} // namespace bspvision
