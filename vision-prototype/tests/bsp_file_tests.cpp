#include "bsp_file.hpp"
#include "heightfield.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

void putU16(QByteArray &bytes, qsizetype offset, quint16 value) {
    qToLittleEndian<quint16>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void putI16(QByteArray &bytes, qsizetype offset, qint16 value) {
    qToLittleEndian<qint16>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void putU32(QByteArray &bytes, qsizetype offset, quint32 value) {
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void putI32(QByteArray &bytes, qsizetype offset, qint32 value) {
    qToLittleEndian<qint32>(value, reinterpret_cast<uchar *>(bytes.data() + offset));
}

void putF32(QByteArray &bytes, qsizetype offset, float value) {
    putU32(bytes, offset, std::bit_cast<quint32>(value));
}

QByteArray makeSyntheticBsp() {
    QByteArray bytes(bspvision::BspFile::HeaderSize, '\0');
    putU32(bytes, 0, 0x50534256U); // VBSP
    putI32(bytes, 4, bspvision::BspFile::SupportedVersion);

    QByteArray vertices(36, '\0');
    putF32(vertices, 0, 0.0F);   putF32(vertices, 4, 0.0F);   putF32(vertices, 8, 0.0F);
    putF32(vertices, 12, 128.0F); putF32(vertices, 16, 0.0F);   putF32(vertices, 20, 0.0F);
    putF32(vertices, 24, 0.0F); putF32(vertices, 28, 128.0F); putF32(vertices, 32, 0.0F);

    QByteArray edges(12, '\0');
    putU16(edges, 0, 0); putU16(edges, 2, 1);
    putU16(edges, 4, 1); putU16(edges, 6, 2);
    putU16(edges, 8, 2); putU16(edges, 10, 0);

    QByteArray surfEdges(12, '\0');
    putI32(surfEdges, 0, 0);
    putI32(surfEdges, 4, 1);
    putI32(surfEdges, 8, 2);

    QByteArray faces(112, '\0');
    putI32(faces, 4, 0);     // firstedge
    putI16(faces, 8, 3);     // numedges
    putI16(faces, 10, 0);    // non-sky texinfo
    putI16(faces, 12, -1);   // no displacement
    putI32(faces, 56 + 4, 0);
    putI16(faces, 56 + 8, 3);
    putI16(faces, 56 + 10, 1); // sky texinfo
    putI16(faces, 56 + 12, -1);

    QByteArray texInfo(144, '\0');
    putI32(texInfo, 68, 0);        // wall material -> texdata 0
    putI32(texInfo, 72 + 68, 1);   // sky material -> texdata 1

    QByteArray texData(64, '\0');
    putI32(texData, 12, 0);        // string-table ID for wall
    putI32(texData, 32 + 12, 1);   // string-table ID for sky

    QByteArray textureStrings;
    textureStrings.append("brick/wall01");
    textureStrings.append('\0');
    const qint32 skyNameOffset = static_cast<qint32>(textureStrings.size());
    textureStrings.append("tools/toolsskybox");
    textureStrings.append('\0');

    QByteArray textureStringTable(8, '\0');
    putI32(textureStringTable, 0, 0);
    putI32(textureStringTable, 4, skyNameOffset);

    const auto addLump = [&bytes](int index, const QByteArray &lump) {
        const qint32 offset = static_cast<qint32>(bytes.size());
        bytes.append(lump);
        const qsizetype headerOffset = 8 + static_cast<qsizetype>(index) * 16;
        putI32(bytes, headerOffset, offset);
        putI32(bytes, headerOffset + 4, static_cast<qint32>(lump.size()));
        putI32(bytes, headerOffset + 8, 0);
        putU32(bytes, headerOffset + 12, 0);
    };

    addLump(2, texData);
    addLump(3, vertices);
    addLump(6, texInfo);
    addLump(7, faces);
    addLump(12, edges);
    addLump(13, surfEdges);
    addLump(43, textureStrings);
    addLump(44, textureStringTable);
    return bytes;
}

QByteArray makeSyntheticDisplacementBsp(quint16 triangleTag = 1U) {
    QByteArray bytes(bspvision::BspFile::HeaderSize, '\0');
    putU32(bytes, 0, 0x50534256U);
    putI32(bytes, 4, bspvision::BspFile::SupportedVersion);

    QByteArray vertices(4 * 12, '\0');
    putF32(vertices, 0, 0.0F);     putF32(vertices, 4, 0.0F);     putF32(vertices, 8, 0.0F);
    putF32(vertices, 12, 128.0F);  putF32(vertices, 16, 0.0F);    putF32(vertices, 20, 0.0F);
    putF32(vertices, 24, 128.0F);  putF32(vertices, 28, 128.0F);  putF32(vertices, 32, 0.0F);
    putF32(vertices, 36, 0.0F);    putF32(vertices, 40, 128.0F);  putF32(vertices, 44, 0.0F);

    QByteArray edges(4 * 4, '\0');
    putU16(edges, 0, 0);  putU16(edges, 2, 1);
    putU16(edges, 4, 1);  putU16(edges, 6, 2);
    putU16(edges, 8, 2);  putU16(edges, 10, 3);
    putU16(edges, 12, 3); putU16(edges, 14, 0);

    QByteArray surfEdges(4 * 4, '\0');
    putI32(surfEdges, 0, 0);
    putI32(surfEdges, 4, 1);
    putI32(surfEdges, 8, 2);
    putI32(surfEdges, 12, 3);

    QByteArray faces(56, '\0');
    putI32(faces, 4, 0);
    putI16(faces, 8, 4);
    putI16(faces, 10, -1);
    putI16(faces, 12, 0);

    QByteArray dispInfo(176, '\0');
    putF32(dispInfo, 0, 0.0F);
    putF32(dispInfo, 4, 0.0F);
    putF32(dispInfo, 8, 0.0F);
    putI32(dispInfo, 12, 0);  // displacement vertex start
    putI32(dispInfo, 16, 0);  // displacement triangle start
    putI32(dispInfo, 20, 2);  // power = 2 -> 5x5 vertices
    putU16(dispInfo, 36, 0);  // parent face index

    const int side = 5;
    QByteArray dispVerts(side * side * 20, '\0');
    for (int i = 0; i < side * side; ++i) {
        putF32(dispVerts, i * 20 + 8, 1.0F); // vertical displacement vector
        putF32(dispVerts, i * 20 + 12, i == 12 ? 8.0F : 0.0F);
        putF32(dispVerts, i * 20 + 16, 0.0F); // alpha
    }

    const int triangleCount = 4 * 4 * 2;
    QByteArray dispTris(triangleCount * 2, '\0');
    for (int i = 0; i < triangleCount; ++i)
        putU16(dispTris, i * 2, triangleTag); // displacement triangle tag

    const auto addLump = [&bytes](int index, const QByteArray &lump) {
        if (lump.isEmpty()) return;
        const qint32 offset = static_cast<qint32>(bytes.size());
        bytes.append(lump);
        const qsizetype headerOffset = 8 + static_cast<qsizetype>(index) * 16;
        putI32(bytes, headerOffset, offset);
        putI32(bytes, headerOffset + 4, static_cast<qint32>(lump.size()));
        putI32(bytes, headerOffset + 8, 0);
        putU32(bytes, headerOffset + 12, 0);
    };

    addLump(3, vertices);
    addLump(7, faces);
    addLump(12, edges);
    addLump(13, surfEdges);
    addLump(26, dispInfo);
    addLump(33, dispVerts);
    addLump(48, dispTris);
    return bytes;
}

bool writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(contents) == contents.size();
}

int fail(const char *message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) return fail("could not create a temporary directory");

    const QString path = temporaryDirectory.filePath(QStringLiteral("synthetic.bsp"));
    const QByteArray validBytes = makeSyntheticBsp();
    if (!writeFile(path, validBytes)) return fail("could not write synthetic BSP");

    bspvision::BspFile bsp;
    QString error;
    if (!bsp.load(path, &error)) {
        std::cerr << "FAIL: valid synthetic BSP was rejected: "
                  << error.toStdString() << '\n';
        return 1;
    }
    if (bsp.vertices().size() != 3) return fail("expected three vertices");
    if (bsp.edges().size() != 3) return fail("expected three edges");
    if (bsp.surfEdges().size() != 3) return fail("expected three surf edges");
    if (bsp.faces().size() != 2) return fail("expected two faces with different materials");
    if (bsp.skyFaceCount() != 1) return fail("expected one sky-textured face");

    const auto polygons = bsp.facePolygons();
    if (polygons.size() != 1 || polygons.first().vertices.size() != 3)
        return fail("default geometry should exclude the sky-textured face");
    if (polygons.first().skySurface || polygons.first().materialName != QStringLiteral("brick/wall01"))
        return fail("visible face material was not resolved correctly");
    const auto allPolygons = bsp.facePolygons(true);
    if (allPolygons.size() != 2 || !allPolygons.last().skySurface ||
        allPolygons.last().materialName != QStringLiteral("tools/toolsskybox"))
        return fail("includeSkySurfaces should retain and classify the sky face");
    if (polygons.first().vertices.at(1).x != 128.0F)
        return fail("face vertex coordinates were decoded incorrectly");

    const QString displacementPath = temporaryDirectory.filePath(QStringLiteral("synthetic-displacement.bsp"));
    if (!writeFile(displacementPath, makeSyntheticDisplacementBsp()))
        return fail("could not write synthetic displacement BSP");
    bspvision::BspFile displaced;
    if (!displaced.load(displacementPath, &error)) {
        std::cerr << "FAIL: valid synthetic displacement BSP was rejected: "
                  << error.toStdString() << '\n';
        return 1;
    }
    if (displaced.displacementCount() != 1 || displaced.displacementTriangleCount() != 32)
        return fail("expected one 5x5 displacement surface with 32 rendered triangles");
    const auto terrain = displaced.facePolygons();
    if (terrain.size() != 32)
        return fail("displacement base face should be replaced by its rendered triangles");
    float maximumTerrainZ = -1000.0F;
    for (const auto &triangle : terrain) {
        if (triangle.vertices.size() != 3)
            return fail("displacement geometry should be emitted as individual triangles");
        for (const auto &vertex : triangle.vertices)
            maximumTerrainZ = std::max(maximumTerrainZ, vertex.z);
    }
    if (std::abs(maximumTerrainZ - 8.0F) > 0.01F)
        return fail("displacement vertex offset was not applied");

    bspvision::Heightfield heightfield;
    if (!heightfield.build(terrain, 8.0, 64, &error)) {
        std::cerr << "FAIL: reconstructed displacement did not rasterize into heightfield: "
                  << error.toStdString() << '\n';
        return 1;
    }
    if (!heightfield.isValid() || heightfield.coveredCellCount() < 100)
        return fail("displacement triangles did not cover the expected heightfield cells");

    // Some real-world BSPs contain displacement triangle records with zero tags.
    // The entire displacement must still render when no triangle has SURFACE set.
    const QString untaggedPath = temporaryDirectory.filePath(QStringLiteral("synthetic-untagged-displacement.bsp"));
    if (!writeFile(untaggedPath, makeSyntheticDisplacementBsp(0U)))
        return fail("could not write untagged displacement BSP");
    bspvision::BspFile untagged;
    if (!untagged.load(untaggedPath, &error)) {
        std::cerr << "FAIL: untagged displacement BSP was rejected: "
                  << error.toStdString() << '\n';
        return 1;
    }
    if (untagged.displacementCount() != 1 || untagged.displacementTriangleCount() != 32 ||
        untagged.displacementFallbackSurfaceCount() != 1 ||
        untagged.displacementTriangleZeroTagCount() != 32)
        return fail("zero-tag displacement fallback did not emit all non-removed triangles");
    if (!heightfield.build(untagged.facePolygons(), 8.0, 64, &error) ||
        heightfield.coveredCellCount() < 100)
        return fail("untagged displacement did not flow through to the heightfield");

    QByteArray badSignature = validBytes;
    badSignature[0] = 'X';
    if (!writeFile(path, badSignature)) return fail("could not write invalid signature test file");
    if (bsp.load(path, &error)) return fail("invalid VBSP signature was accepted");

    QByteArray badBounds = validBytes;
    putI32(badBounds, 8 + 7 * 16, static_cast<qint32>(badBounds.size() + 100));
    if (!writeFile(path, badBounds)) return fail("could not write invalid lump-range test file");
    if (bsp.load(path, &error)) return fail("out-of-range lump was accepted");

    std::cout << "PASS: BSP v20 header, geometry parsing, face reconstruction, sky material filtering, and invalid-input checks\n";
    return 0;
}
