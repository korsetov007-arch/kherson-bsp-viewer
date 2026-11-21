#include "live_overlay.hpp"
#include "app_language.hpp"
#include "world_landmark.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QShowEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bspvision {
namespace {
double wrapDegrees(double degrees) {
    while (degrees > 180.0) degrees -= 360.0;
    while (degrees < -180.0) degrees += 360.0;
    return degrees;
}
}

LiveOverlay::LiveOverlay(QWidget *parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Source BSP Geometry Lab overlay"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
}

void LiveOverlay::showOverlayOn(const QRect &geometry) {
    setGeometry(geometry);
    show();
    raise();
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        style | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
#endif
    update();
}

void LiveOverlay::setCurrentAngles(double yawDeg, double sourcePitchDeg) {
    currentYaw_ = yawDeg;
    currentPitch_ = sourcePitchDeg;
    update();
}

void LiveOverlay::setDesiredAngles(double yawDeg, double sourcePitchDeg) {
    desiredYaw_ = yawDeg;
    desiredPitch_ = sourcePitchDeg;
    update();
}

void LiveOverlay::setHorizontalFov(double fovDeg) {
    horizontalFov_ = std::clamp(fovDeg, 30.0, 140.0);
    update();
}

void LiveOverlay::setTrackingStatus(const QString &status) {
    trackingStatus_ = status;
    update();
}

void LiveOverlay::setTargetDetails(double range, double flightSeconds, const QString &mapName) {
    range_ = range;
    flightSeconds_ = flightSeconds;
    mapName_ = mapName;
    update();
}

void LiveOverlay::setTargetEnabled(bool enabled) {
    if (enabled != targetEnabled_) {
        hasSmoothedTargetScreen_ = false;
        targetSmoothingClock_.invalidate();
        targetLocked_ = false;
    }
    targetEnabled_ = enabled;
    update();
}

void LiveOverlay::setLandmark(const QVector3D &worldPosition,
                              const QVector3D &cameraPosition,
                              double yawDeg, double sourcePitchDeg, bool enabled) {
    if (enabled && !landmarkEnabled_) {
        hasSmoothedLandmarkScreen_ = false;
        landmarkSmoothingClock_.invalidate();
    }
    if (!enabled) {
        hasSmoothedLandmarkScreen_ = false;
        landmarkSmoothingClock_.invalidate();
    }
    landmarkWorldPosition_ = worldPosition;
    cameraPosition_ = cameraPosition;
    landmarkYaw_ = yawDeg;
    landmarkPitch_ = sourcePitchDeg;
    landmarkEnabled_ = enabled;
    update();
}

void LiveOverlay::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        style | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

void LiveOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    // Explicitly clear old pixels when the game loses focus or the overlay is disabled.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (!targetEnabled_ && !landmarkEnabled_) return;

    painter.setRenderHint(QPainter::Antialiasing, true);
    const double screenW = width();
    const double screenH = height();
    const double cx = screenW * 0.5;
    const double cy = screenH * 0.5;
    const double hfov = qDegreesToRadians(horizontalFov_);
    const double vfov = 2.0 * std::atan(std::tan(hfov * 0.5) * screenH / std::max(1.0, screenW));
    const QColor panel(12, 18, 26, 210);

    if (targetEnabled_) {
        const double yawDelta = wrapDegrees(desiredYaw_ - currentYaw_);
        const double pitchDelta = desiredPitch_ - currentPitch_;
        double projectedX = cx;
        double projectedY = cy;
        bool offScreen = false;
        const bool safelyInFront = std::abs(yawDelta) < 88.0 && std::abs(pitchDelta) < 88.0;
        if (safelyInFront) {
            projectedX = cx - std::tan(qDegreesToRadians(yawDelta)) /
                std::tan(hfov * 0.5) * cx;
            projectedY = cy + std::tan(qDegreesToRadians(pitchDelta)) /
                std::tan(vfov * 0.5) * cy;
        } else {
            const double dx = yawDelta / std::max(1.0, horizontalFov_ * 0.5);
            const double dy = pitchDelta / std::max(1.0, qRadiansToDegrees(vfov) * 0.5);
            const double scale = std::max({std::abs(dx), std::abs(dy), 0.001});
            projectedX = cx - (dx / scale) * (cx - 38.0);
            projectedY = cy + (dy / scale) * (cy - 72.0);
            offScreen = true;
        }

        const bool inViewport = safelyInFront && projectedX >= 0.0 &&
            projectedX < screenW && projectedY >= 0.0 && projectedY < screenH;
        if (!inViewport) offScreen = true;
        const double margin = 32.0;
        const QPointF requestedPoint(
            std::clamp(projectedX, margin, screenW - margin),
            std::clamp(projectedY, margin, screenH - margin));

        // Smooth the aim marker independently of the 30 Hz telemetry sampler.
        // The time-based filter keeps motion visually continuous at different refresh rates.
        if (!targetSmoothingClock_.isValid()) {
            targetSmoothingClock_.start();
            smoothedTargetScreen_ = requestedPoint;
            hasSmoothedTargetScreen_ = true;
        } else {
            const qint64 elapsedMs = std::clamp<qint64>(
                targetSmoothingClock_.restart(), 1, 100);
            const double alpha = 1.0 - std::exp(-static_cast<double>(elapsedMs) / 46.0);
            if (!hasSmoothedTargetScreen_) {
                smoothedTargetScreen_ = requestedPoint;
                hasSmoothedTargetScreen_ = true;
            } else {
                smoothedTargetScreen_ += (requestedPoint - smoothedTargetScreen_) * alpha;
            }
        }

        // Small hysteresis prevents the lock colour from flickering at the edge.
        const double rawErrorPixels = std::hypot(projectedX - cx, projectedY - cy);
        if (!targetLocked_ && inViewport && rawErrorPixels <= 19.0)
            targetLocked_ = true;
        else if (targetLocked_ && (!inViewport || rawErrorPixels > 29.0))
            targetLocked_ = false;

        const QPointF marker = smoothedTargetScreen_;
        const QColor accent = targetLocked_ ? QColor(66, 246, 151, 250)
            : (offScreen ? QColor(255, 171, 76, 248) : QColor(255, 205, 99, 248));
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 34),
                            9.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(cx, cy), marker);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 86),
                            2.0, Qt::DashLine, Qt::RoundCap));
        painter.drawLine(QPointF(cx, cy), marker);

        // Layered rings and separated arc segments read cleanly over bright or dark scenes.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 28),
                            10.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(marker, 24.0, 24.0);
        painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 92),
                            4.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(marker, 21.0, 21.0);
        painter.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(QRectF(marker.x() - 18.0, marker.y() - 18.0, 36.0, 36.0),
                        24 * 16, 132 * 16);
        painter.drawArc(QRectF(marker.x() - 18.0, marker.y() - 18.0, 36.0, 36.0),
                        204 * 16, 132 * 16);
        painter.setPen(QPen(QColor(245, 250, 255, 205), 1.1, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(marker, 13.5, 13.5);

        painter.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(marker + QPointF(-30.0, 0.0), marker + QPointF(-23.0, 0.0));
        painter.drawLine(marker + QPointF(23.0, 0.0), marker + QPointF(30.0, 0.0));
        painter.drawLine(marker + QPointF(0.0, -30.0), marker + QPointF(0.0, -23.0));
        painter.drawLine(marker + QPointF(0.0, 23.0), marker + QPointF(0.0, 30.0));
        painter.setPen(QPen(QColor(247, 250, 255, 225), 1.2));
        painter.drawLine(QPointF(cx - 9.0, cy), QPointF(cx + 9.0, cy));
        painter.drawLine(QPointF(cx, cy - 9.0), QPointF(cx, cy + 9.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(marker, targetLocked_ ? 4.5 : 3.4, targetLocked_ ? 4.5 : 3.4);

        if (offScreen) {
            painter.setPen(QPen(accent, 2.6, Qt::SolidLine, Qt::RoundCap));
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(marker + QPointF(-7.0, -7.0), marker + QPointF(7.0, 7.0));
            painter.drawLine(marker + QPointF(-7.0, 7.0), marker + QPointF(7.0, -7.0));
        }
        painter.restore();

        const QString aimLabel = targetLocked_
            ? uiText("BALLISTIC LOCK", "БАЛЛИСТИКА · НАВЕДЕНО")
            : (offScreen ? uiText("TURN TO AIM MARKER", "ПОВЕРНИТЕСЬ К МАРКЕРУ")
                         : uiText("BALLISTIC AIM", "БАЛЛИСТИЧЕСКАЯ НАВОДКА"));
        painter.setPen(QColor(245, 248, 252, 242));
        painter.drawText(QRectF(marker.x() + 36.0, marker.y() - 13.0, 220.0, 26.0),
                         Qt::AlignLeft | Qt::AlignVCenter, aimLabel);
    }

    if (landmarkEnabled_) {
        QPointF landmarkScreen;
        double depth = 0.0;
        const QSize viewport(qRound(screenW), qRound(screenH));
        const bool inFront = projectWorldPointToScreen(
            landmarkWorldPosition_, cameraPosition_, landmarkYaw_, landmarkPitch_,
            horizontalFov_, viewport, &landmarkScreen, &depth);
        if (inFront) {
            const bool inViewport = landmarkScreen.x() >= 0.0 && landmarkScreen.x() < screenW &&
                                    landmarkScreen.y() >= 0.0 && landmarkScreen.y() < screenH;

            // Exponential smoothing is time-based, so the marker remains stable
            // across different refresh rates and the 30 Hz cl_showpos sampling.
            if (!landmarkSmoothingClock_.isValid()) {
                landmarkSmoothingClock_.start();
                smoothedLandmarkScreen_ = landmarkScreen;
                hasSmoothedLandmarkScreen_ = true;
            } else {
                const qint64 elapsedMs = std::clamp<qint64>(
                    landmarkSmoothingClock_.restart(), 1, 100);
                const double alpha = 1.0 - std::exp(-static_cast<double>(elapsedMs) / 58.0);
                if (!hasSmoothedLandmarkScreen_) {
                    smoothedLandmarkScreen_ = landmarkScreen;
                    hasSmoothedLandmarkScreen_ = true;
                } else {
                    smoothedLandmarkScreen_ += (landmarkScreen - smoothedLandmarkScreen_) * alpha;
                }
            }

            const double margin = 30.0;
            const QPointF displayPoint(
                std::clamp(smoothedLandmarkScreen_.x(), margin, screenW - margin),
                std::clamp(smoothedLandmarkScreen_.y(), margin, screenH - margin));
            constexpr double aimRadius = 22.0;
            const bool aimedAt = inViewport &&
                std::hypot(landmarkScreen.x() - cx, landmarkScreen.y() - cy) <= aimRadius;
            const QColor ring = aimedAt ? QColor(57, 246, 137, 250)
                                        : QColor(255, 190, 58, 248);

            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            if (inViewport) {
                painter.setPen(QPen(QColor(ring.red(), ring.green(), ring.blue(), 58),
                                    1.2, Qt::DashLine, Qt::RoundCap));
                painter.drawLine(QPointF(cx, cy), displayPoint);
            }

            // Layered translucent strokes create a clean glow without a costly blur.
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(ring.red(), ring.green(), ring.blue(), 24),
                                10.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawEllipse(displayPoint, 25.0, 25.0);
            painter.setPen(QPen(QColor(ring.red(), ring.green(), ring.blue(), 78),
                                5.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawEllipse(displayPoint, 23.0, 23.0);
            painter.setPen(QPen(ring, 2.4, Qt::SolidLine, Qt::RoundCap));
            painter.drawEllipse(displayPoint, aimRadius, aimRadius);
            painter.setPen(QPen(QColor(245, 250, 255, 190), 1.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawEllipse(displayPoint, 16.5, 16.5);

            painter.setPen(QPen(ring, 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(displayPoint + QPointF(-31.0, 0.0),
                             displayPoint + QPointF(-25.0, 0.0));
            painter.drawLine(displayPoint + QPointF(25.0, 0.0),
                             displayPoint + QPointF(31.0, 0.0));
            painter.drawLine(displayPoint + QPointF(0.0, -31.0),
                             displayPoint + QPointF(0.0, -25.0));
            painter.drawLine(displayPoint + QPointF(0.0, 25.0),
                             displayPoint + QPointF(0.0, 31.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(ring);
            painter.drawEllipse(displayPoint, aimedAt ? 4.2 : 3.2, aimedAt ? 4.2 : 3.2);

            if (!inViewport) {
                painter.setPen(QPen(ring, 2.6, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(displayPoint + QPointF(-7.0, -7.0),
                                 displayPoint + QPointF(7.0, 7.0));
                painter.drawLine(displayPoint + QPointF(-7.0, 7.0),
                                 displayPoint + QPointF(7.0, -7.0));
            }
            painter.restore();

            painter.setPen(QColor(245, 248, 252, 238));
            const QString label = aimedAt
                ? uiText("AIM LOCK", "НАВЕДЕНО")
                : (inViewport ? uiText("LANDMARK", "ОРИЕНТИР")
                              : uiText("LANDMARK · TURN", "ОРИЕНТИР · ПОВОРОТ"));
            painter.drawText(QRectF(displayPoint.x() + 35.0,
                                    displayPoint.y() - 12.0, 190.0, 24.0),
                             Qt::AlignLeft | Qt::AlignVCenter, label);
        } else {
            painter.setPen(QPen(QColor(255, 190, 55, 245), 2.5));
            painter.drawText(QRectF(24.0, cy - 16.0, 240.0, 32.0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             uiText("LANDMARK BEHIND", "ОРИЕНТИР ПОЗАДИ"));
        }
    }

    const QRectF panelRect(18.0, screenH - 122.0, 430.0, 100.0);
    painter.setPen(QPen(QColor(65, 83, 100, 235), 1.0));
    painter.setBrush(panel);
    painter.drawRoundedRect(panelRect, 7.0, 7.0);
    painter.setPen(QColor(90, 210, 255));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 8, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     uiText("SOURCE BSP GEOMETRY LAB  ·  EXTERNAL OVERLAY",
                            "SOURCE BSP GEOMETRY LAB  ·  ВНЕШНИЙ ОВЕРЛЕЙ"));
    painter.setPen(targetEnabled_ ? (targetLocked_ ? QColor(66, 246, 151) : QColor(255, 190, 75))
                                  : (landmarkEnabled_ ? QColor(255, 190, 55) : QColor(89, 235, 176)));
    const QString instruction = targetEnabled_
        ? (targetLocked_
            ? uiText("Ballistic aim: LOCK · F7 toggles", "Баллистика: НАВЕДЕНО · F7 включает/выключает")
            : uiText("Ballistic aim: follow the smooth marker", "Баллистика: следуйте за плавным маркером"))
        : (landmarkEnabled_
            ? uiText("Aiming ring: GREEN = centered · F7 toggles", "Круг наведения: ЗЕЛЁНЫЙ = наведено · F7 включает/выключает")
            : uiText("Trajectory aim indicator", "Индикатор наведения по траектории"));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 29, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, instruction);
    painter.setPen(QColor(235, 239, 245));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 50, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     uiText("Range %1 units  ·  flight %2 s", "Дальность %1 ед.  ·  полёт %2 с")
                         .arg(range_, 0, 'f', 0).arg(flightSeconds_, 0, 'f', 2));
    painter.setPen(QColor(184, 197, 211));
    const QString mapLabel = mapName_.isEmpty()
        ? uiText("BSP map not specified", "BSP-карта не выбрана")
        : uiText("Map: ", "Карта: ") + mapName_;
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 72, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, mapLabel + QStringLiteral("  ·  ") + trackingStatus_);
}

} // namespace bspvision
