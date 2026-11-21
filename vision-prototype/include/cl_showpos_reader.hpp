#pragma once

#include <QImage>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <memory>

namespace bspvision {

struct ClShowPosSample {
    QVector3D position;
    QVector3D angles; // Source order: pitch, yaw, roll.
    double speed = 0.0;
    double confidence = 0.0;
    bool hasSpeed = false;
    QString positionLine;
    QString anglesLine;
    QString velocityLine;
};

class ClShowPosReader final {
public:
    ClShowPosReader();
    ~ClShowPosReader();

    ClShowPosReader(const ClShowPosReader &) = delete;
    ClShowPosReader &operator=(const ClShowPosReader &) = delete;

    bool start(const QRect &region, QString *error = nullptr);
    void stop();
    bool isRunning() const;

    // Returns true only when a fresh, validated sample was decoded.
    // The supplied sample is left unchanged when no complete sample is available.
    bool poll(ClShowPosSample *sample = nullptr, QString *status = nullptr);
    QString lastStatus() const;
    bool latestSample(ClShowPosSample *sample) const;

    // Kept separate from screen capture so the numeric parser can be tested
    // without a running game or access to a desktop duplication device.
    static bool parseTextLines(const QStringList &lines,
                               ClShowPosSample *sample,
                               QString *error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bspvision
