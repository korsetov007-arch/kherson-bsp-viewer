#include "raw_mouse_tracker.hpp"

#include <QCoreApplication>
#include <QDateTime>

#include <algorithm>
#include <iterator>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bspvision {

bool RawMouseTracker::initialize(quintptr targetWindowHandle) {
#ifdef Q_OS_WIN
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01; // Generic desktop controls
    device.usUsage = 0x02;     // Mouse
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = reinterpret_cast<HWND>(targetWindowHandle);
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        lastError_ = QStringLiteral("RegisterRawInputDevices failed (Win32 error %1).")
            .arg(GetLastError());
        return false;
    }
    if (auto *app = QCoreApplication::instance())
        app->installNativeEventFilter(this);
    initialized_ = true;
    lastError_.clear();
    return true;
#else
    Q_UNUSED(targetWindowHandle);
    lastError_ = QStringLiteral("Raw mouse tracking is only implemented on Windows.");
    return false;
#endif
}

void RawMouseTracker::start(double initialYawDeg, double initialSourcePitchDeg,
                            double sensitivity, double mYaw, double mPitch) {
    yawDeg_ = initialYawDeg;
    sourcePitchDeg_ = std::clamp(initialSourcePitchDeg, -89.0, 89.0);
    setCalibration(sensitivity, mYaw, mPitch);
    lastInputTimestampMs_ = 0;
    enabled_ = initialized_;
    if (!initialized_) {
        lastError_ = QStringLiteral("Raw input is not initialized.");
    } else {
        lastError_.clear();
    }
}

void RawMouseTracker::stop() {
    enabled_ = false;
    gameForeground_ = false;
}

void RawMouseTracker::setCalibration(double sensitivity, double mYaw, double mPitch) {
    sensitivity_ = std::clamp(sensitivity, 0.01, 100.0);
    mYaw_ = std::clamp(mYaw, -10.0, 10.0);
    mPitch_ = std::clamp(mPitch, -10.0, 10.0);
}

bool RawMouseTracker::isGModForeground() {
#ifdef Q_OS_WIN
    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        cachedForegroundWindow_ = 0;
        gameForeground_ = false;
        return false;
    }
    const quintptr handleValue = reinterpret_cast<quintptr>(foreground);
    if (handleValue == cachedForegroundWindow_)
        return gameForeground_;

    cachedForegroundWindow_ = handleValue;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (processId == 0) {
        gameForeground_ = false;
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        gameForeground_ = false;
        return false;
    }

    wchar_t imagePath[32768]{};
    DWORD pathLength = static_cast<DWORD>(std::size(imagePath));
    const BOOL queried = QueryFullProcessImageNameW(process, 0, imagePath, &pathLength);
    CloseHandle(process);

    wchar_t windowTitle[512]{};
    GetWindowTextW(foreground, windowTitle, static_cast<int>(std::size(windowTitle)));
    const QString path = queried ? QString::fromWCharArray(imagePath, static_cast<qsizetype>(pathLength)).toLower() : QString();
    const QString title = QString::fromWCharArray(windowTitle).toLower();
    const bool isSourceExecutable = path.endsWith(QStringLiteral("/hl2.exe"))
        || path.endsWith(QStringLiteral("\\hl2.exe"));
    const bool identifiesGMod = path.contains(QStringLiteral("garrysmod"))
        || path.contains(QStringLiteral("garry's mod"))
        || title.contains(QStringLiteral("garry's mod"))
        || title.contains(QStringLiteral("garrys mod"));
    gameForeground_ = isSourceExecutable && identifiesGMod;
    return gameForeground_;
#else
    gameForeground_ = false;
    return false;
#endif
}

bool RawMouseTracker::refreshForegroundState() {
    return isGModForeground();
}

bool RawMouseTracker::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType);
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (!enabled_ || !message) return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_INPUT) return false;

    RAWINPUT input{};
    UINT inputSize = static_cast<UINT>(sizeof(input));
    const UINT copied = GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT,
                                        &input, &inputSize, sizeof(RAWINPUTHEADER));
    if (copied == static_cast<UINT>(-1) || input.header.dwType != RIM_TYPEMOUSE)
        return false;

    if (!isGModForeground()) return false;
    if ((input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) return false;

    const LONG dx = input.data.mouse.lLastX;
    const LONG dy = input.data.mouse.lLastY;
    if (dx == 0 && dy == 0) return false;

    // Source-style mouse look: horizontal movement subtracts yaw; vertical movement adds pitch.
    yawDeg_ -= static_cast<double>(dx) * sensitivity_ * mYaw_;
    while (yawDeg_ > 180.0) yawDeg_ -= 360.0;
    while (yawDeg_ < -180.0) yawDeg_ += 360.0;
    sourcePitchDeg_ = std::clamp(
        sourcePitchDeg_ + static_cast<double>(dy) * sensitivity_ * mPitch_, -89.0, 89.0);

    lastInputTimestampMs_ = QDateTime::currentMSecsSinceEpoch();
    if (anglesChanged) anglesChanged(yawDeg_, sourcePitchDeg_);
#else
    Q_UNUSED(message);
#endif
    return false;
}

} // namespace bspvision
