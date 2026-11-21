#include "live_overlay.hpp"
#include "app_language.hpp"

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
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (!targetEnabled_) return;

    // Keep a compact status panel only. Do not draw target rings, crosshairs,
    // projected aim points, arrows, or guide lines over the game.
    const double screenH = height();
    const QRectF panelRect(18.0, screenH - 88.0, 500.0, 66.0);
    painter.setPen(QPen(QColor(65, 83, 100, 220), 1.0));
    painter.setBrush(QColor(12, 18, 26, 190));
    painter.drawRoundedRect(panelRect, 7.0, 7.0);

    painter.setPen(QColor(90, 210, 255));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 7,
                            panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     uiText("BALLISTIC AUTO AIM", "БАЛЛИСТИЧЕСКАЯ АВТОСУО"));

    painter.setPen(QColor(235, 239, 245));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 28,
                            panelRect.width() - 22, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     trackingStatus_.left(100));
    painter.setPen(QColor(184, 197, 211));
    painter.drawText(QRectF(panelRect.x() + 11, panelRect.y() + 46,
                            panelRect.width() - 22, 16),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     uiText("F7: toggle auto aim", "F7: включить / выключить автодоводку"));
}

} // namespace bspvision
