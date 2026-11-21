#include "live_overlay.hpp"

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
    targetEnabled_ = enabled;
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
    if (!targetEnabled_) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double screenW = width();
    const double screenH = height();
    const double cx = screenW * 0.5;
    const double cy = screenH * 0.5;
    const double hfov = qDegreesToRadians(horizontalFov_);
    const double vfov = 2.0 * std::atan(std::tan(hfov * 0.5) * screenH / std::max(1.0, screenW));
    const double yawDelta = wrapDegrees(desiredYaw_ - currentYaw_);
    const double pitchDelta = desiredPitch_ - currentPitch_;

    double markerX = cx;
    double markerY = cy;
    bool offScreen = false;
    const bool safelyInFront = std::abs(yawDelta) < 88.0 && std::abs(pitchDelta) < 88.0;
    if (safelyInFront) {
        markerX = cx + std::tan(qDegreesToRadians(yawDelta)) / std::tan(hfov * 0.5) * cx;
        markerY = cy + std::tan(qDegreesToRadians(pitchDelta)) / std::tan(vfov * 0.5) * cy;
    } else {
        // Behind or near 90 degrees: show the direction on the screen edge without tan() flipping sign.
        const double dx = yawDelta / std::max(1.0, horizontalFov_ * 0.5);
        const double dy = pitchDelta / std::max(1.0, qRadiansToDegrees(vfov) * 0.5);
        const double scale = std::max({std::abs(dx), std::abs(dy), 0.001});
        markerX = cx + (dx / scale) * (cx - 34.0);
        markerY = cy + (dy / scale) * (cy - 70.0);
        offScreen = true;
    }

    const double margin = 28.0;
    if (markerX < margin || markerX > screenW - margin ||
        markerY < margin || markerY > screenH - margin) {
        offScreen = true;
    }
    markerX = std::clamp(markerX, margin, screenW - margin);
    markerY = std::clamp(markerY, margin, screenH - margin);

    const QColor accent = offScreen ? QColor(255, 181, 69, 245) : QColor(89, 235, 176, 245);
    const QColor panel(12, 18, 26, 210);
    painter.setPen(QPen(QColor(232, 240, 247, 185), 1.4));
    painter.drawLine(QPointF(cx - 10, cy), QPointF(cx + 10, cy));
    painter.drawLine(QPointF(cx, cy - 10), QPointF(cx, cy + 10));
    painter.setPen(QPen(accent, 2.2));
    painter.drawLine(QPointF(cx, cy), QPointF(markerX, markerY));
    painter.drawEllipse(QPointF(markerX, markerY), 11.0, 11.0);
    painter.drawLine(QPointF(markerX - 17, markerY), QPointF(markerX - 5, markerY));
    painter.drawLine(QPointF(markerX + 5, markerY), QPointF(markerX + 17, markerY));
    painter.drawLine(QPointF(markerX, markerY - 17), QPointF(markerX, markerY - 5));
    painter.drawLine(QPointF(markerX, markerY + 5), QPointF(markerX, markerY + 17));

    const QRectF panelRect(18.0, screenH - 122.0, 430.0, 100.0);
    painter.setPen(QPen(QColor(65, 83, 100, 235), 1.0));
    painter.setBrush(panel);
    painter.drawRoundedRect(panelRect, 7.0, 7.0);
    painter.setPen(QColor(90, 210, 255));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 8, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("SOURCE BSP GEOMETRY LAB  ·  EXTERNAL OVERLAY"));
    painter.setPen(accent);
    const QString instruction = offScreen
        ? QStringLiteral("TURN  ·  Δ yaw %1°  ·  Δ pitch %2°").arg(yawDelta, 0, 'f', 1).arg(pitchDelta, 0, 'f', 1)
        : QStringLiteral("Δ yaw %1°  ·  Δ pitch %2°").arg(yawDelta, 0, 'f', 1).arg(pitchDelta, 0, 'f', 1);
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 29, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, instruction);
    painter.setPen(QColor(235, 239, 245));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 50, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Range %1 units  ·  flight %2 s").arg(range_, 0, 'f', 0).arg(flightSeconds_, 0, 'f', 2));
    painter.setPen(QColor(184, 197, 211));
    const QString mapLabel = mapName_.isEmpty() ? QStringLiteral("BSP map not specified") : QStringLiteral("Map: ") + mapName_;
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 72, panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, mapLabel + QStringLiteral("  ·  ") + trackingStatus_);
}

} // namespace bspvision
