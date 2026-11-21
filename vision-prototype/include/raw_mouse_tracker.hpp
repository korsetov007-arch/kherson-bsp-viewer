#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QString>

#include <functional>

namespace bspvision {

class RawMouseTracker final : public QAbstractNativeEventFilter {
public:
    bool initialize(quintptr targetWindowHandle);
    void start(double initialYawDeg, double initialSourcePitchDeg,
               double sensitivity, double mYaw, double mPitch);
    void stop();

    void setCalibration(double sensitivity, double mYaw, double mPitch);
    double yawDeg() const { return yawDeg_; }
    double sourcePitchDeg() const { return sourcePitchDeg_; }
    bool enabled() const { return enabled_; }
    bool gameForeground() const { return gameForeground_; }
    qint64 lastInputTimestampMs() const { return lastInputTimestampMs_; }
    QString lastError() const { return lastError_; }

    std::function<void(double, double)> anglesChanged;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    bool isGModForeground();

    double yawDeg_ = 0.0;
    double sourcePitchDeg_ = 0.0;
    double sensitivity_ = 3.0;
    double mYaw_ = 0.022;
    double mPitch_ = 0.022;
    bool initialized_ = false;
    bool enabled_ = false;
    bool gameForeground_ = false;
    quintptr cachedForegroundWindow_ = 0;
    qint64 lastInputTimestampMs_ = 0;
    QString lastError_;
};

} // namespace bspvision
