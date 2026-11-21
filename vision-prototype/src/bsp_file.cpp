#include "bsp_file.hpp"
#include "app_language.hpp"

#include <QFile>
#include <QtEndian>

#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

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

bool isSkyTexture(const QString &materialName) {
    const QString name = materialName.trimmed().toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
    return name.startsWith(QStringLiteral("tools/toolsskybox")) ||
           name.contains(QStringLiteral("/toolsskybox")) ||
           name.startsWith(QStringLiteral("skybox/")) ||
           name.startsWith(QStringLiteral("sky/")) ||
           name.startsWith(QStringLiteral("sky_"));
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
        if (error) *error = uiText("Cannot open file: ", "Не удалось открыть файл: ") + file.errorString();
        return false;
    }
    bytes_ = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error) *error = uiText("Failed while reading BSP: ", "Ошибка при чтении BSP: ") + file.errorString();
        clear();
        return false;
    }
    if (bytes_.size() < HeaderSize) {
        if (error) *error = uiText("File is smaller than the Source BSP header.", "Файл меньше заголовка Source BSP.");
        clear();
        return false;
    }

    const auto *data = reinterpret_cast<const uchar *>(bytes_.constData());
    // Bytes "VBSP" have this value when interpreted as a little-endian uint32.
    if (readU32(data) != 0x50534256U) {
        if (error) *error = uiText("Invalid BSP signature. Expected VBSP.", "Неверная сигнатура BSP. Ожидалась VBSP.");
        clear();
        return false;
    }

    version_ = readI32(data + 4);
    if (version_ != SupportedVersion) {
        if (error) {
            *error = uiText("Unsupported BSP version %1; this build supports version %2.", "Версия BSP %1 не поддерживается; эта сборка поддерживает версию %2.")
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
            if (error) *error = uiText("Lump %1 has a negative offset or length.", "Lump %1 имеет отрицательное смещение или длину.").arg(lump.name);
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
            if (error) *error = uiText("Lump %1 points outside the file.", "Lump %1 выходит за пределы файла.").arg(lump.name);
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
        if (error) *error = uiText("Core geometry lumps have unexpected record sizes.", "Неверный размер записей в основных геометрических lump.");
        clear();
        return false;
    }

    vertices_.reserve(vertexLump.length / 12);
    for (qint32 offset = vertexLump.offset; offset < vertexLump.offset + vertexLump.length; offset += 12) {
        const float x = readF32(data + offset);
        const float y = readF32(data + offset + 4);
        const float z = readF32(data + offset + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            if (error) *error = uiText("Vertex lump contains a non-finite coordinate.", "Lump вершин содержит некорректные координаты.");
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

    // Resolve each texture-info record to a material name. Source BSP faces reference
    // texinfo -> texdata -> string-table ID -> NUL-terminated texture name.
    const auto &texDataLump = lumps_[2];
    const auto &texInfoLump = lumps_[6];
    const auto &textureStringDataLump = lumps_[43];
    const auto &textureStringTableLump = lumps_[44];

    QVector<qint32> texDataForInfo;
    if ((texInfoLump.length % 72) == 0) {
        texDataForInfo.reserve(texInfoLump.length / 72);
        for (qint32 offset = texInfoLump.offset; offset < texInfoLump.offset + texInfoLump.length; offset += 72)
            texDataForInfo.push_back(readI32(data + offset + 68));
    }

    QVector<qint32> stringOffsets;
    if ((textureStringTableLump.length % 4) == 0) {
        stringOffsets.reserve(textureStringTableLump.length / 4);
        for (qint32 offset = textureStringTableLump.offset;
             offset < textureStringTableLump.offset + textureStringTableLump.length; offset += 4)
            stringOffsets.push_back(readI32(data + offset));
    }

    QVector<QString> texDataNames;
    if ((texDataLump.length % 32) == 0) {
        texDataNames.reserve(texDataLump.length / 32);
        for (qint32 offset = texDataLump.offset; offset < texDataLump.offset + texDataLump.length; offset += 32) {
            const qint32 stringTableId = readI32(data + offset + 12);
            QString materialName;
            if (stringTableId >= 0 && stringTableId < stringOffsets.size()) {
                const qint32 nameOffset = stringOffsets[stringTableId];
                if (nameOffset >= 0 && nameOffset < textureStringDataLump.length) {
                    const char *nameStart = reinterpret_cast<const char *>(
                        data + textureStringDataLump.offset + nameOffset);
                    const qint64 remaining = textureStringDataLump.length - nameOffset;
                    const void *terminator = std::memchr(nameStart, '\\0', static_cast<size_t>(remaining));
                    const qsizetype nameLength = terminator
                        ? static_cast<qsizetype>(static_cast<const char *>(terminator) - nameStart)
                        : static_cast<qsizetype>(remaining);
                    materialName = QString::fromLatin1(nameStart, nameLength);
                }
            }
            texDataNames.push_back(materialName);
        }
    }

    faces_.reserve(faceLump.length / 56);
    for (qint32 offset = faceLump.offset; offset < faceLump.offset + faceLump.length; offset += 56) {
        BspFace face;
        face.firstEdge = readI32(data + offset + 4);
        face.edgeCount = readI16(data + offset + 8);
        face.textureInfo = readI16(data + offset + 10);
        face.displacementInfo = readI16(data + offset + 12);
        const qint16 texInfoIndex = face.textureInfo;
        if (texInfoIndex >= 0 && texInfoIndex < texDataForInfo.size()) {
            const qint32 texDataIndex = texDataForInfo[texInfoIndex];
            if (texDataIndex >= 0 && texDataIndex < texDataNames.size()) {
                face.materialName = texDataNames[texDataIndex];
                face.skySurface = isSkyTexture(face.materialName);
            }
        }
        faces_.push_back(face);
    }

    filePath_ = path;
    loaded_ = true;
    return true;
}

QVector<BspPolygon> BspFile::facePolygons(bool includeSkySurfaces) const {
    QVector<BspPolygon> result;
    if (!loaded_) return result;
    result.reserve(faces_.size());

    for (const BspFace &face : faces_) {
        if (face.skySurface && !includeSkySurfaces) continue;
        if (face.edgeCount < 3 || face.edgeCount > 4096 || face.firstEdge < 0)
            continue;
        const qint64 edgeEnd = static_cast<qint64>(face.firstEdge) + face.edgeCount;
        if (edgeEnd > surfEdges_.size()) continue;

        BspPolygon polygon;
        polygon.materialName = face.materialName;
        polygon.skySurface = face.skySurface;
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

int BspFile::skyFaceCount() const {
    int count = 0;
    for (const BspFace &face : faces_) if (face.skySurface) ++count;
    return count;
}

QString BspFile::summary() const {
    if (!loaded_) return uiText("No BSP loaded.", "BSP-карта не загружена.");
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
        .arg(validPolygons)
        .arg(skyFaceCount());
}

} // namespace bspvision
