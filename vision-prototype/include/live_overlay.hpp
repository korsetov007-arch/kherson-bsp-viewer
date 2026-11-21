#pragma once

#include <QWidget>
#include <QRect>
#include <QElapsedTimer>
#include <QPointF>
#include <QString>

namespace bspvision {

class LiveOverlay final : public QWidget {
public:
    explicit LiveOverlay(QWidget *parent = nullptr);

    void showOverlayOn(const QRect &geometry);
    void setCurrentAngles(double yawDeg, double sourcePitchDeg);
    void setDesiredAngles(double yawDeg, double sourcePitchDeg);
    void setHorizontalFov(double fovDeg);
    void setTrackingStatus(const QString &status);
    void setTargetDetails(double range, double flightSeconds, const QString &mapName);
    void setTargetEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    double currentYaw_ = 0.0;
    double currentPitch_ = 0.0;
    double desiredYaw_ = 0.0;
    double desiredPitch_ = 0.0;
    double horizontalFov_ = 90.0;
    double range_ = 0.0;
    double flightSeconds_ = 0.0;
    QString trackingStatus_ = QStringLiteral("Tracking not started");
    QString mapName_;
    bool targetEnabled_ = false;
    bool targetLocked_ = false;
    QPointF smoothedTargetScreen_;
    bool hasSmoothedTargetScreen_ = false;
    QElapsedTimer targetSmoothingClock_;
};

} // namespace bspvision
