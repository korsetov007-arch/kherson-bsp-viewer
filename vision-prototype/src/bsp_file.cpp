#include "bsp_file.hpp"
#include "app_language.hpp"

#include <QFile>
#include <QtEndian>

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
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
           name.startsWith(QStringLiteral("tools/tool_skybox")) ||
           name.contains(QStringLiteral("/toolsskybox")) ||
           name.contains(QStringLiteral("/tool_skybox")) ||
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
    displacementPolygonsByFace_.clear();
    displacementFacePresent_.clear();
    displacementCount_ = 0;
    displacementTriangleCount_ = 0;
    displacementVertexCount_ = 0;
    displacementTriangleRecordCount_ = 0;
    displacementTriangleSurfaceTagCount_ = 0;
    displacementTriangleZeroTagCount_ = 0;
    displacementTriangleRemovedCount_ = 0;
    displacementFallbackSurfaceCount_ = 0;
    displacementPowerCounts_.fill(0);
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
                    const void *terminator = std::memchr(nameStart, '\0', static_cast<size_t>(remaining));
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

    const auto &dispInfoLump = lumps_[26];
    const auto &dispVertLump = lumps_[33];
    const auto &dispTriLump = lumps_[48];
    if ((dispInfoLump.length % 176) != 0 ||
        (dispVertLump.length % 20) != 0 ||
        (dispTriLump.length % 2) != 0) {
        if (error) *error = uiText(
            "Displacement lumps have unexpected record sizes.",
            "Некорректные размеры записей displacement-lump.");
        clear();
        return false;
    }

    displacementPolygonsByFace_.resize(faces_.size());
    displacementFacePresent_.fill(0, faces_.size());

    const auto resolveFaceVertices = [this](const BspFace &face,
                                            QVector<BspVertex> *out) -> bool {
        if (!out || face.edgeCount < 3 || face.edgeCount > 4096 || face.firstEdge < 0)
            return false;
        const qint64 edgeEnd = static_cast<qint64>(face.firstEdge) + face.edgeCount;
        if (edgeEnd > surfEdges_.size()) return false;
        out->clear();
        out->reserve(face.edgeCount);
        for (qint32 i = 0; i < face.edgeCount; ++i) {
            const qint32 signedEdge = surfEdges_[face.firstEdge + i];
            const qint64 edgeIndex = signedEdge < 0 ? -static_cast<qint64>(signedEdge) : signedEdge;
            if (edgeIndex < 0 || edgeIndex >= edges_.size()) return false;
            const BspEdge &edge = edges_[static_cast<qsizetype>(edgeIndex)];
            const quint16 vertexIndex = signedEdge < 0 ? edge.second : edge.first;
            if (vertexIndex >= vertices_.size()) return false;
            out->push_back(vertices_[vertexIndex]);
        }
        return out->size() >= 3;
    };

    const qint32 dispVertRecordCount = dispVertLump.length / 20;
    const qint32 dispTriRecordCount = dispTriLump.length / 2;
    const qint32 dispInfoRecordCount = dispInfoLump.length / 176;
    constexpr quint16 kDispTriSurface = 1U << 0;
    constexpr quint16 kDispTriRemove = 1U << 5;

    for (qint32 dispIndex = 0; dispIndex < dispInfoRecordCount; ++dispIndex) {
        const uchar *info = data + dispInfoLump.offset + static_cast<qint64>(dispIndex) * 176;
        const qint32 vertStart = readI32(info + 12);
        const qint32 triStart = readI32(info + 16);
        const qint32 power = readI32(info + 20);
        const quint16 faceIndex = readU16(info + 36);

        if (power < 2 || power > 4) {
            if (error) *error = uiText(
                "Displacement %1 has unsupported power %2; expected 2–4.",
                "Displacement %1 имеет неподдерживаемый power %2; ожидался диапазон 2–4.")
                .arg(dispIndex).arg(power);
            clear();
            return false;
        }

        const qint32 divisions = 1 << power;
        const qint32 side = divisions + 1;
        const qint32 vertexCount = side * side;
        const qint32 triangleCount = divisions * divisions * 2;
        if (vertStart < 0 || static_cast<qint64>(vertStart) + vertexCount > dispVertRecordCount ||
            triStart < 0 || static_cast<qint64>(triStart) + triangleCount > dispTriRecordCount) {
            if (error) *error = uiText(
                "Displacement %1 references vertices or triangles outside their lumps.",
                "Displacement %1 ссылается на вершины или треугольники за пределами lump.")
                .arg(dispIndex);
            clear();
            return false;
        }

        ++displacementPowerCounts_[static_cast<size_t>(power - 2)];
        displacementVertexCount_ += vertexCount;
        displacementTriangleRecordCount_ += triangleCount;

        if (static_cast<quint32>(faceIndex) >= static_cast<quint32>(faces_.size())) continue;
        const BspFace &baseFace = faces_[faceIndex];
        if (baseFace.displacementInfo != dispIndex || baseFace.edgeCount != 4) continue;

        QVector<BspVertex> corners;
        if (!resolveFaceVertices(baseFace, &corners) || corners.size() != 4) continue;

        const double startX = readF32(info);
        const double startY = readF32(info + 4);
        const double startZ = readF32(info + 8);
        if (!std::isfinite(startX) || !std::isfinite(startY) || !std::isfinite(startZ)) {
            if (error) *error = uiText(
                "Displacement %1 has a non-finite start position.",
                "Displacement %1 содержит некорректную стартовую позицию.").arg(dispIndex);
            clear();
            return false;
        }

        int startCorner = 0;
        double bestDistanceSquared = std::numeric_limits<double>::max();
        for (int i = 0; i < corners.size(); ++i) {
            const double dx = corners[i].x - startX;
            const double dy = corners[i].y - startY;
            const double dz = corners[i].z - startZ;
            const double distanceSquared = dx * dx + dy * dy + dz * dz;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                startCorner = i;
            }
        }
        if (bestDistanceSquared > 1.0) continue;

        const BspVertex &p00 = corners[startCorner];
        const BspVertex &p10 = corners[(startCorner + 1) % 4];
        const BspVertex &p11 = corners[(startCorner + 2) % 4];
        const BspVertex &p01 = corners[(startCorner + 3) % 4];

        QVector<BspVertex> grid;
        grid.resize(vertexCount);
        for (qint32 row = 0; row < side; ++row) {
            const double s = static_cast<double>(row) / divisions;
            for (qint32 col = 0; col < side; ++col) {
                const double t = static_cast<double>(col) / divisions;
                const double w00 = (1.0 - s) * (1.0 - t);
                const double w10 = (1.0 - s) * t;
                const double w11 = s * t;
                const double w01 = s * (1.0 - t);
                const double baseX = w00 * p00.x + w10 * p10.x + w11 * p11.x + w01 * p01.x;
                const double baseY = w00 * p00.y + w10 * p10.y + w11 * p11.y + w01 * p01.y;
                const double baseZ = w00 * p00.z + w10 * p10.z + w11 * p11.z + w01 * p01.z;

                const qint32 vertexIndex = row * side + col;
                const uchar *dispVert = data + dispVertLump.offset +
                    static_cast<qint64>(vertStart + vertexIndex) * 20;
                const double vectorX = readF32(dispVert);
                const double vectorY = readF32(dispVert + 4);
                const double vectorZ = readF32(dispVert + 8);
                const double distance = readF32(dispVert + 12);
                if (!std::isfinite(vectorX) || !std::isfinite(vectorY) ||
                    !std::isfinite(vectorZ) || !std::isfinite(distance)) {
                    if (error) *error = uiText(
                        "Displacement %1 contains a non-finite displacement vertex.",
                        "Displacement %1 содержит некорректную вершину.").arg(dispIndex);
                    clear();
                    return false;
                }

                grid[vertexIndex] = {
                    static_cast<float>(baseX + vectorX * distance),
                    static_cast<float>(baseY + vectorY * distance),
                    static_cast<float>(baseZ + vectorZ * distance)
                };
            }
        }

        // Triangle tags describe surface/physics properties, but some valid BSPs
        // leave the SURFACE bit clear for every triangle. In that case, don't
        // drop the entire displacement: keep every triangle not explicitly marked
        // REMOVE. If SURFACE is present anywhere, honour it alongside REMOVE.
        QVector<quint16> triangleTags;
        triangleTags.reserve(triangleCount);
        qint32 surfaceTaggedTriangles = 0;
        for (qint32 triIndex = 0; triIndex < triangleCount; ++triIndex) {
            const uchar *triData = data + dispTriLump.offset +
                static_cast<qint64>(triStart + triIndex) * 2;
            const quint16 tags = readU16(triData);
            triangleTags.push_back(tags);
            if ((tags & kDispTriSurface) != 0) ++surfaceTaggedTriangles;
            if (tags == 0) ++displacementTriangleZeroTagCount_;
            if ((tags & kDispTriSurface) != 0) ++displacementTriangleSurfaceTagCount_;
            if ((tags & kDispTriRemove) != 0) ++displacementTriangleRemovedCount_;
        }
        const bool useSurfaceTagFilter = surfaceTaggedTriangles > 0;
        if (!useSurfaceTagFilter) ++displacementFallbackSurfaceCount_;

        auto &renderTriangles = displacementPolygonsByFace_[faceIndex];
        displacementFacePresent_[faceIndex] = 1;
        for (qint32 row = 0; row < divisions; ++row) {
            for (qint32 col = 0; col < divisions; ++col) {
                const qint32 a = row * side + col;
                const qint32 b = a + 1;
                const qint32 c = a + side;
                const qint32 d = c + 1;
                const qint32 firstTri = (row * divisions + col) * 2;
                // Grid row follows p00 -> p10 and column follows p00 -> p01.
                // Keep triangle winding aligned with the parent BSP face so
                // upward-facing terrain is also accepted by the heightfield rasterizer.
                const qint32 triIndices[2][3] = {{a, c, b}, {b, c, d}};
                for (qint32 half = 0; half < 2; ++half) {
                    const qint32 triIndex = firstTri + half;
                    const quint16 tags = triangleTags[triIndex];
                    if ((tags & kDispTriRemove) != 0) continue;
                    if (useSurfaceTagFilter && (tags & kDispTriSurface) == 0) continue;

                    BspPolygon triangle;
                    triangle.materialName = baseFace.materialName;
                    triangle.skySurface = baseFace.skySurface;
                    triangle.vertices.reserve(3);
                    triangle.vertices.push_back(grid[triIndices[half][0]]);
                    triangle.vertices.push_back(grid[triIndices[half][1]]);
                    triangle.vertices.push_back(grid[triIndices[half][2]]);
                    renderTriangles.push_back(std::move(triangle));
                    ++displacementTriangleCount_;
                }
            }
        }
        ++displacementCount_;
    }

    filePath_ = path;
    loaded_ = true;
    return true;
}

QVector<BspPolygon> BspFile::facePolygons(bool includeSkySurfaces) const {
    QVector<BspPolygon> result;
    if (!loaded_) return result;
    result.reserve(faces_.size() + displacementTriangleCount_);

    for (qsizetype faceIndex = 0; faceIndex < faces_.size(); ++faceIndex) {
        const BspFace &face = faces_[faceIndex];
        if (face.skySurface && !includeSkySurfaces) continue;
        if (faceIndex < displacementFacePresent_.size() && displacementFacePresent_[faceIndex] != 0) {
            const auto &triangles = displacementPolygonsByFace_[faceIndex];
            for (const BspPolygon &triangle : triangles) result.push_back(triangle);
            continue;
        }
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
    const QVector<BspPolygon> visiblePolygons = facePolygons();
    const int validPolygons = visiblePolygons.size();
    qint64 totalSurfaceTriangles = 0;
    for (const BspPolygon &polygon : visiblePolygons) {
        if (polygon.vertices.size() >= 3)
            totalSurfaceTriangles += polygon.vertices.size() - 2;
    }
    return uiText(
        "File: %1\n"
        "Format: Source BSP v%2 | Size: %3 MiB\n"
        "Vertices: %4 | Edges: %5 | Surf edges: %6\n"
        "Faces in lump: %7 | Visible polygons (sky surfaces hidden): %8 | Sky faces: %9\n"
        "Triangulated visible surface: %10 triangles\n"
        "Displacements: %11 | Displacement vertices: %12 | DispTri records: %13\n"
        "Displacement triangles rendered: %14 | SURFACE-tagged: %15 | zero-tag: %16 | REMOVE-tagged: %17 | untagged fallback surfaces: %18\n"
        "Displacement power: p2=%19, p3=%20, p4=%21",
        "Файл: %1\n"
        "Формат: Source BSP v%2 | Размер: %3 МиБ\n"
        "Вершины: %4 | Рёбра: %5 | Направленные рёбра: %6\n"
        "Граней в lump: %7 | Видимых полигонов (без скайбокса): %8 | Граней sky-материалов: %9\n"
        "Треугольников всей видимой поверхности: %10\n"
        "Displacement-поверхностей: %11 | Вершин displacement: %12 | Записей DispTri: %13\n"
        "Треугольников displacement отрисовано: %14 | с SURFACE: %15 | с нулевым тегом: %16 | с REMOVE: %17 | поверхностей с fallback: %18\n"
        "Распределение power: p2=%19, p3=%20, p4=%21")
        .arg(filePath_)
        .arg(version_)
        .arg(static_cast<double>(fileSize()) / (1024.0 * 1024.0), 0, 'f', 2)
        .arg(vertices_.size())
        .arg(edges_.size())
        .arg(surfEdges_.size())
        .arg(faces_.size())
        .arg(validPolygons)
        .arg(skyFaceCount())
        .arg(totalSurfaceTriangles)
        .arg(displacementCount_)
        .arg(displacementVertexCount_)
        .arg(displacementTriangleRecordCount_)
        .arg(displacementTriangleCount_)
        .arg(displacementTriangleSurfaceTagCount_)
        .arg(displacementTriangleZeroTagCount_)
        .arg(displacementTriangleRemovedCount_)
        .arg(displacementFallbackSurfaceCount_)
        .arg(displacementPowerCounts_[0])
        .arg(displacementPowerCounts_[1])
        .arg(displacementPowerCounts_[2]);
}
} // namespace bspvision
