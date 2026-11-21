#include "cl_showpos_reader.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <iterator>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

namespace bspvision {
namespace {

QString numberPattern() {
    return QStringLiteral("[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)");
}

bool parseNumberTokens(const QString &line, QVector<double> *numbers) {
    if (!numbers) return false;
    numbers->clear();
    const qsizetype colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0) return false;
    const QString body = line.mid(colon + 1);
    const QRegularExpression expression(numberPattern());
    auto it = expression.globalMatch(body);
    while (it.hasNext() && numbers->size() < 4) {
        const auto match = it.next();
        bool ok = false;
        const double value = match.captured().toDouble(&ok);
        if (!ok || !std::isfinite(value)) return false;
        numbers->push_back(value);
    }
    return !numbers->isEmpty();
}

QImage thresholdWhiteText(const QImage &source) {
    const QImage converted = source.convertToFormat(QImage::Format_ARGB32);
    QImage mask(source.size(), QImage::Format_Grayscale8);
    if (mask.isNull()) return {};

    // cl_showpos uses bright, nearly neutral text. Requiring all channels to
    // be bright rejects most coloured HUD elements and game-scene highlights.
    constexpr int threshold = 168;
    for (int y = 0; y < converted.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(converted.constScanLine(y));
        uchar *dst = mask.scanLine(y);
        for (int x = 0; x < converted.width(); ++x) {
            const QRgb pixel = src[x];
            const bool white = qRed(pixel) >= threshold &&
                               qGreen(pixel) >= threshold &&
                               qBlue(pixel) >= threshold;
            dst[x] = white ? 255 : 0;
        }
    }
    return mask;
}

int countInk(const QImage &mask) {
    int count = 0;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) count += row[x] != 0 ? 1 : 0;
    }
    return count;
}

struct FontProfile {
    QString family;
    int pixelSize = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    int ascent = 0;
    QHash<QChar, QImage> glyphs;
};

QImage renderGlyph(const QFont &font, QChar character, int cellWidth, int cellHeight, int ascent) {
    QImage rendered(cellWidth, cellHeight, QImage::Format_ARGB32);
    rendered.fill(qRgb(0, 0, 0));
    QPainter painter(&rendered);
    painter.setRenderHint(QPainter::TextAntialiasing, false);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::white);
    painter.setFont(font);
    painter.drawText(0, ascent, QString(character));
    painter.end();

    QImage mask(cellWidth, cellHeight, QImage::Format_Grayscale8);
    for (int y = 0; y < cellHeight; ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(rendered.constScanLine(y));
        uchar *dst = mask.scanLine(y);
        for (int x = 0; x < cellWidth; ++x)
            dst[x] = qGray(src[x]) >= 128 ? 255 : 0;
    }
    return mask;
}

bool makeFontProfile(const QString &family, int pixelSize, FontProfile *out) {
    if (!out) return false;

    QFont font(family);
    font.setPixelSize(pixelSize);
    font.setFixedPitch(true);
    font.setStyleHint(QFont::TypeWriter);
    font.setHintingPreference(QFont::PreferFullHinting);

    QImage metricsDevice(64, 64, QImage::Format_ARGB32);
    QFontMetrics metrics(font, &metricsDevice);
    const int width = metrics.horizontalAdvance(QLatin1Char('0'));
    const int height = metrics.height();
    if (width < 4 || width > 18 || height < 7 || height > 28) return false;

    FontProfile profile;
    profile.family = QFontInfo(font).family();
    profile.pixelSize = pixelSize;
    profile.cellWidth = width;
    profile.cellHeight = height;
    profile.ascent = metrics.ascent();

    const QString alphabet = QStringLiteral("0123456789.+-:posangvel");
    for (const QChar character : alphabet) {
        if (!profile.glyphs.contains(character))
            profile.glyphs.insert(character, renderGlyph(font, character, width, height, profile.ascent));
    }
    profile.glyphs.insert(QLatin1Char(' '), QImage(width, height, QImage::Format_Grayscale8));
    profile.glyphs[QLatin1Char(' ')].fill(0);

    *out = std::move(profile);
    return true;
}

QImage renderTemplate(const FontProfile &profile, const QString &text) {
    if (text.isEmpty() || profile.cellWidth <= 0 || profile.cellHeight <= 0) return {};
    QImage result(text.size() * profile.cellWidth, profile.cellHeight, QImage::Format_Grayscale8);
    result.fill(0);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto it = profile.glyphs.constFind(text.at(i));
        if (it == profile.glyphs.cend()) continue;
        const QImage &glyph = it.value();
        for (int y = 0; y < profile.cellHeight; ++y) {
            uchar *dst = result.scanLine(y) + i * profile.cellWidth;
            const uchar *src = glyph.constScanLine(y);
            std::memcpy(dst, src, static_cast<size_t>(profile.cellWidth));
        }
    }
    return result;
}

// Dice distance between binary masks: 0 is an exact match, 1 is no overlap.
double maskDistance(const QImage &observed, int x, int y, const QImage &templ) {
    if (x < 0 || y < 0 || x + templ.width() > observed.width() ||
        y + templ.height() > observed.height()) {
        return 1.0;
    }

    int observedInk = 0;
    int templateInk = 0;
    int intersection = 0;
    for (int row = 0; row < templ.height(); ++row) {
        const uchar *a = observed.constScanLine(y + row) + x;
        const uchar *b = templ.constScanLine(row);
        for (int col = 0; col < templ.width(); ++col) {
            const bool inkA = a[col] != 0;
            const bool inkB = b[col] != 0;
            observedInk += inkA ? 1 : 0;
            templateInk += inkB ? 1 : 0;
            intersection += (inkA && inkB) ? 1 : 0;
        }
    }
    const int denominator = observedInk + templateInk;
    if (denominator == 0) return 1.0;
    return 1.0 - (2.0 * static_cast<double>(intersection) / denominator);
}

struct LabelMatch {
    int x = -1;
    int y = -1;
    double distance = 1.0;
};

LabelMatch findLabel(const QImage &observed, const FontProfile &profile,
                     const QString &label, int maxX, int maxY,
                     int minX = 0, int minY = 0) {
    LabelMatch best;
    const QImage templ = renderTemplate(profile, label);
    if (templ.isNull() || templ.width() > observed.width() || templ.height() > observed.height())
        return best;

    const int lastX = std::min(maxX, observed.width() - templ.width());
    const int lastY = std::min(maxY, observed.height() - templ.height());
    for (int y = std::max(0, minY); y <= lastY; ++y) {
        for (int x = std::max(0, minX); x <= lastX; ++x) {
            const double distance = maskDistance(observed, x, y, templ);
            if (distance < best.distance) best = {x, y, distance};
            if (best.distance < 0.025) return best;
        }
    }
    return best;
}

struct DecodedLine {
    QString text;
    double confidence = 0.0;
};

DecodedLine decodeLine(const QImage &mask, const FontProfile &profile, int x, int y,
                       const QString &prefix, int maxCells) {
    DecodedLine result;
    result.text = prefix;
    const int numericStart = x + prefix.size() * profile.cellWidth;
    if (numericStart < 0 || y < 0 || numericStart >= mask.width() ||
        y + profile.cellHeight > mask.height())
        return result;

    // The label is already anchored by a literal template. Decode only the
    // numeric tail; this avoids spending time classifying scene pixels beyond
    // the three values and reduces false positives from bright scenery.
    const QString alphabet = QStringLiteral("0123456789.+-");
    const int count = std::min(maxCells, (mask.width() - numericStart) / profile.cellWidth);
    double totalDistance = 0.0;
    int classified = 0;

    for (int cell = 0; cell < count; ++cell) {
        const int left = numericStart + cell * profile.cellWidth;
        int ink = 0;
        for (int row = 0; row < profile.cellHeight; ++row) {
            const uchar *pixels = mask.constScanLine(y + row) + left;
            for (int col = 0; col < profile.cellWidth; ++col) ink += pixels[col] != 0 ? 1 : 0;
        }
        if (ink == 0) {
            result.text.append(QLatin1Char(' '));
            continue;
        }

        QChar bestCharacter = QLatin1Char('?');
        double bestDistance = 1.0;
        for (const QChar candidate : alphabet) {
            const auto it = profile.glyphs.constFind(candidate);
            if (it == profile.glyphs.cend()) continue;

            double distance = 1.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    distance = std::min(distance, maskDistance(mask, left + dx, y + dy, it.value()));
                }
            }
            if (distance < bestDistance) {
                bestDistance = distance;
                bestCharacter = candidate;
            }
        }

        if (bestDistance > 0.58) bestCharacter = QLatin1Char('?');
        result.text.append(bestCharacter);
        if (bestCharacter != QLatin1Char('?')) {
            totalDistance += bestDistance;
            ++classified;
        }
    }
    result.confidence = classified > 0
        ? std::clamp(1.0 - totalDistance / static_cast<double>(classified), 0.0, 1.0)
        : 0.0;
    return result;
}

class TemplateMatcher final {
public:
    bool decode(const QImage &frame, ClShowPosSample *sample, QString *error) {
        if (!sample || frame.isNull()) {
            if (error) *error = QStringLiteral("Desktop capture returned an empty frame.");
            return false;
        }
        const QImage mask = thresholdWhiteText(frame);
        if (mask.isNull()) {
            if (error) *error = QStringLiteral("Could not allocate the text mask.");
            return false;
        }

        ++framesSeen_;
        if (!calibrated_) {
            // First attempt immediately, then retry only occasionally. A missing
            // HUD must not trigger dozens of font/position scans per second.
            if (framesSeen_ != 1 && (framesSeen_ % 30) != 1) {
                if (error) *error = QStringLiteral(
                    "Waiting for a cl_showpos template match. Enable cl_showpos 1 and adjust the capture rectangle if needed.");
                return false;
            }
            if (!calibrate(mask)) {
                if (error) *error = QStringLiteral(
                    "No confident cl_showpos template match in this crop. Enable cl_showpos 1 and adjust the capture rectangle.");
                return false;
            }
        }

        const int lineStep = profile_.cellHeight + 2;
        const QImage angleLabel = renderTemplate(profile_, QStringLiteral("ang:"));
        const double angleLabelDistance = maskDistance(
            mask, anchor_.x, anchor_.y + lineStep, angleLabel);
        if (angleLabelDistance > 0.64) {
            if (error) *error = QStringLiteral("The pos: label was found, but the ang: line did not match the expected template.");
            return false;
        }

        const DecodedLine pos = decodeLine(mask, profile_, anchor_.x, anchor_.y,
                                           QStringLiteral("pos:"), 38);
        const DecodedLine ang = decodeLine(mask, profile_, anchor_.x, anchor_.y + lineStep,
                                           QStringLiteral("ang:"), 38);
        const QImage velocityLabel = renderTemplate(profile_, QStringLiteral("vel:"));
        const int velocityY = anchor_.y + 2 * lineStep;
        const bool hasVelocityLabel = velocityY + profile_.cellHeight <= mask.height() &&
            maskDistance(mask, anchor_.x, velocityY, velocityLabel) <= 0.64;
        const DecodedLine vel = hasVelocityLabel
            ? decodeLine(mask, profile_, anchor_.x, velocityY, QStringLiteral("vel:"), 16)
            : DecodedLine{QStringLiteral("vel:"), 0.0};
        QString parseError;
        ClShowPosSample parsed;
        if (!ClShowPosReader::parseTextLines({pos.text, ang.text, vel.text}, &parsed, &parseError)) {
            if ((framesSeen_ % 30) == 0) calibrated_ = false;
            if (error) *error = QStringLiteral("Template match found, but numeric validation failed: %1").arg(parseError);
            return false;
        }

        parsed.positionLine = pos.text.trimmed();
        parsed.anglesLine = ang.text.trimmed();
        parsed.velocityLine = vel.text.trimmed();
        parsed.confidence = std::clamp(
            (1.0 - anchor_.distance) * 0.30 +
            (1.0 - angleLabelDistance) * 0.20 +
            (pos.confidence + ang.confidence) * 0.25,
            0.0, 1.0);
        if (parsed.confidence < 0.35) {
            if (error) *error = QStringLiteral("Read confidence is too low; keep the game visible and adjust the crop.");
            return false;
        }

        *sample = std::move(parsed);
        if (error) error->clear();
        return true;
    }

    void reset() { calibrated_ = false; }

private:
    bool calibrate(const QImage &mask) {
        QStringList families = {
            QStringLiteral("DebugFixed"),
            QStringLiteral("Fixedsys"),
            QStringLiteral("Lucida Console"),
            QStringLiteral("Consolas"),
            QStringLiteral("Courier New")
        };
        const QString systemFixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        if (!systemFixed.isEmpty() && !families.contains(systemFixed)) families.append(systemFixed);

        double bestDistance = 1.0;
        FontProfile bestProfile;
        LabelMatch bestAnchor;
        for (const QString &family : families) {
            for (int pixelSize = 8; pixelSize <= 18; ++pixelSize) {
                FontProfile candidate;
                if (!makeFontProfile(family, pixelSize, &candidate)) continue;
                const LabelMatch match = findLabel(mask, candidate, QStringLiteral("pos:"),
                    std::min(220, mask.width()), std::min(34, mask.height()));
                if (match.distance < bestDistance) {
                    bestDistance = match.distance;
                    bestProfile = std::move(candidate);
                    bestAnchor = match;
                }
                if (bestDistance < 0.025) break;
            }
            if (bestDistance < 0.025) break;
        }

        // A low score means the candidate glyphs agree with the captured label.
        // Keep the threshold conservative; false coordinates are worse than no data.
        if (bestAnchor.x < 0 || bestDistance > 0.56) return false;
        profile_ = std::move(bestProfile);
        anchor_ = bestAnchor;
        calibrated_ = true;
        return true;
    }

    FontProfile profile_;
    LabelMatch anchor_;
    bool calibrated_ = false;
    int framesSeen_ = 0;
};

#ifdef Q_OS_WIN
class DesktopDuplicationCapture final {
public:
    enum class Result { Frame, NoUpdate, Error };

    bool start(const QRect &region, QString *error) {
        stop();
        region_ = region;
        if (region.width() < 200 || region.height() < 45 ||
            region.x() < 0 || region.y() < 0) {
            if (error) *error = QStringLiteral("Capture crop must be at least 200×45 pixels and use non-negative screen coordinates.");
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return fail(error, QStringLiteral("CreateDXGIFactory1 failed"), hr);

        Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedAdapter;
        Microsoft::WRL::ComPtr<IDXGIOutput> selectedOutput;
        Microsoft::WRL::ComPtr<IDXGIOutput> fallbackOutput;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> fallbackAdapter;

        for (UINT adapterIndex = 0;; ++adapterIndex) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            for (UINT outputIndex = 0;; ++outputIndex) {
                Microsoft::WRL::ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC desc{};
                if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
                if (!fallbackOutput) {
                    fallbackAdapter = adapter;
                    fallbackOutput = output;
                }
                if (desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0) {
                    selectedAdapter = adapter;
                    selectedOutput = output;
                    break;
                }
            }
            if (selectedOutput) break;
        }
        if (!selectedOutput) {
            selectedAdapter = fallbackAdapter;
            selectedOutput = fallbackOutput;
        }
        if (!selectedAdapter || !selectedOutput) {
            if (error) *error = QStringLiteral("No attached desktop display was found.");
            return false;
        }

        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL createdLevel{};
        hr = D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            flags, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
            &device_, &createdLevel, &context_);
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                flags, levels + 1, static_cast<UINT>(std::size(levels) - 1), D3D11_SDK_VERSION,
                &device_, &createdLevel, &context_);
        }
        if (FAILED(hr)) return fail(error, QStringLiteral("D3D11CreateDevice failed"), hr);

        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        hr = selectedOutput.As(&output1);
        if (FAILED(hr)) return fail(error, QStringLiteral("IDXGIOutput1 query failed"), hr);
        hr = output1->DuplicateOutput(device_.Get(), &duplication_);
        if (FAILED(hr)) return fail(error, QStringLiteral("DXGI DuplicateOutput failed"), hr);

        DXGI_OUTDUPL_DESC desc{};
        duplication_->GetDesc(&desc);
        outputWidth_ = static_cast<int>(desc.ModeDesc.Width);
        outputHeight_ = static_cast<int>(desc.ModeDesc.Height);
        if (region.right() >= outputWidth_ || region.bottom() >= outputHeight_) {
            if (error) *error = QStringLiteral("Crop is outside the primary display (%1×%2); set X/Y and size to match the game monitor.")
                .arg(outputWidth_).arg(outputHeight_);
            stop();
            return false;
        }
        running_ = true;
        if (error) error->clear();
        return true;
    }

    void stop() {
        duplication_.Reset();
        context_.Reset();
        device_.Reset();
        staging_.Reset();
        running_ = false;
        outputWidth_ = outputHeight_ = 0;
    }

    bool isRunning() const { return running_; }

    Result capture(QImage *image, QString *error) {
        if (!running_ || !image) {
            if (error) *error = QStringLiteral("Desktop duplication is not started.");
            return Result::Error;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        Microsoft::WRL::ComPtr<IDXGIResource> resource;
        const HRESULT hr = duplication_->AcquireNextFrame(0, &frameInfo, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return Result::NoUpdate;
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            stop();
            if (error) *error = QStringLiteral("Desktop duplication lost access after a display or fullscreen-mode change; restart the screen reader.");
            return Result::Error;
        }
        if (FAILED(hr)) {
            if (error) *error = QStringLiteral("AcquireNextFrame failed (0x%1).")
                .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
            return Result::Error;
        }

        struct FrameRelease {
            IDXGIOutputDuplication *duplication = nullptr;
            ~FrameRelease() { if (duplication) duplication->ReleaseFrame(); }
        } release{duplication_.Get()};

        Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop;
        const HRESULT textureHr = resource.As(&desktop);
        if (FAILED(textureHr)) {
            if (error) *error = QStringLiteral("Could not obtain the duplicated desktop texture.");
            return Result::Error;
        }

        D3D11_TEXTURE2D_DESC sourceDesc{};
        desktop->GetDesc(&sourceDesc);
        if (!staging_ || stagingFormat_ != sourceDesc.Format) {
            staging_.Reset();
            D3D11_TEXTURE2D_DESC stagingDesc{};
            stagingDesc.Width = static_cast<UINT>(region_.width());
            stagingDesc.Height = static_cast<UINT>(region_.height());
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = sourceDesc.Format;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            const HRESULT createHr = device_->CreateTexture2D(&stagingDesc, nullptr, &staging_);
            if (FAILED(createHr)) {
                if (error) *error = QStringLiteral("Could not allocate the staging texture.");
                return Result::Error;
            }
            stagingFormat_ = sourceDesc.Format;
        }

        const D3D11_BOX crop{
            static_cast<UINT>(region_.x()), static_cast<UINT>(region_.y()), 0,
            static_cast<UINT>(region_.x() + region_.width()),
            static_cast<UINT>(region_.y() + region_.height()), 1
        };
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, desktop.Get(), 0, &crop);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT mapHr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(mapHr)) {
            if (error) *error = QStringLiteral("Could not map the captured crop for CPU reading.");
            return Result::Error;
        }

        QImage captured(region_.size(), QImage::Format_ARGB32);
        for (int y = 0; y < region_.height(); ++y) {
            std::memcpy(captured.scanLine(y),
                static_cast<const uchar *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                static_cast<size_t>(region_.width()) * 4U);
        }
        context_->Unmap(staging_.Get(), 0);
        *image = std::move(captured);
        if (error) error->clear();
        return Result::Frame;
    }

private:
    static bool fail(QString *error, const QString &message, HRESULT hr) {
        if (error) *error = QStringLiteral("%1 (0x%2).")
            .arg(message).arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
        return false;
    }

    QRect region_;
    bool running_ = false;
    int outputWidth_ = 0;
    int outputHeight_ = 0;
    DXGI_FORMAT stagingFormat_ = DXGI_FORMAT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
};
#else
class DesktopDuplicationCapture final {
public:
    enum class Result { Frame, NoUpdate, Error };
    bool start(const QRect &, QString *error) {
        if (error) *error = QStringLiteral("The cl_showpos screen reader currently requires Windows DXGI Desktop Duplication.");
        return false;
    }
    void stop() {}
    bool isRunning() const { return false; }
    Result capture(QImage *, QString *error) {
        if (error) *error = QStringLiteral("DXGI Desktop Duplication is only available on Windows.");
        return Result::Error;
    }
};
#endif

} // namespace

struct ClShowPosReader::Impl {
    DesktopDuplicationCapture capture;
    TemplateMatcher matcher;
    QRect region;
    QString status = QStringLiteral("Screen reader is stopped.");
    ClShowPosSample sample;
    bool hasSample = false;
};

ClShowPosReader::ClShowPosReader() : impl_(std::make_unique<Impl>()) {}
ClShowPosReader::~ClShowPosReader() { stop(); }

bool ClShowPosReader::start(const QRect &region, QString *error) {
    stop();
    impl_->region = region;
    impl_->matcher.reset();
    impl_->hasSample = false;
    if (!impl_->capture.start(region, error)) {
        impl_->status = error ? *error : QStringLiteral("Could not start desktop capture.");
        return false;
    }
    impl_->status = QStringLiteral("Capturing screen crop; waiting for a cl_showpos template match.");
    if (error) error->clear();
    return true;
}

void ClShowPosReader::stop() {
    if (impl_) {
        impl_->capture.stop();
        impl_->status = QStringLiteral("Screen reader is stopped.");
        impl_->hasSample = false;
    }
}

bool ClShowPosReader::isRunning() const {
    return impl_ && impl_->capture.isRunning();
}

bool ClShowPosReader::poll(ClShowPosSample *sample, QString *status) {
    if (!impl_) return false;
    QImage frame;
    QString captureError;
    const auto result = impl_->capture.capture(&frame, &captureError);
    if (result == DesktopDuplicationCapture::Result::Error) {
        impl_->status = captureError;
    } else if (result == DesktopDuplicationCapture::Result::Frame) {
        ClShowPosSample decoded;
        QString decodeError;
        if (impl_->matcher.decode(frame, &decoded, &decodeError)) {
            impl_->sample = std::move(decoded);
            impl_->hasSample = true;
            impl_->status = QStringLiteral("Reading cl_showpos · confidence %1%")
                .arg(qRound(impl_->sample.confidence * 100.0));
            if (sample) *sample = impl_->sample;
            if (status) *status = impl_->status;
            return true;
        }
        impl_->status = decodeError;
    }
    if (status) *status = impl_->status;
    return false;
}

QString ClShowPosReader::lastStatus() const {
    return impl_ ? impl_->status : QStringLiteral("Screen reader is not initialized.");
}

bool ClShowPosReader::latestSample(ClShowPosSample *sample) const {
    if (!impl_ || !impl_->hasSample || !sample) return false;
    *sample = impl_->sample;
    return true;
}

bool ClShowPosReader::parseTextLines(const QStringList &lines,
                                     ClShowPosSample *sample,
                                     QString *error) {
    if (!sample) {
        if (error) *error = QStringLiteral("Sample output pointer is null.");
        return false;
    }

    QString posLine;
    QString angLine;
    QString velLine;
    for (const QString &line : lines) {
        const QString lower = line.toLower();
        if (lower.contains(QStringLiteral("pos:"))) posLine = line;
        else if (lower.contains(QStringLiteral("ang:"))) angLine = line;
        else if (lower.contains(QStringLiteral("vel:"))) velLine = line;
    }
    if (posLine.isEmpty() || angLine.isEmpty()) {
        if (error) *error = QStringLiteral("Both pos: and ang: lines are required.");
        return false;
    }

    QVector<double> position;
    QVector<double> angles;
    if (!parseNumberTokens(posLine, &position) || position.size() < 3 ||
        !parseNumberTokens(angLine, &angles) || angles.size() < 3) {
        if (error) *error = QStringLiteral("Could not parse three numeric coordinates and three numeric angles.");
        return false;
    }

    constexpr double maxWorldCoordinate = 10000000.0;
    constexpr double maxAngleMagnitude = 100000.0;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(position[i]) > maxWorldCoordinate ||
            std::abs(angles[i]) > maxAngleMagnitude) {
            if (error) *error = QStringLiteral("Decoded values are outside plausible Source coordinate/angle ranges.");
            return false;
        }
    }

    ClShowPosSample parsed;
    parsed.position = QVector3D(static_cast<float>(position[0]), static_cast<float>(position[1]),
                                static_cast<float>(position[2]));
    parsed.angles = QVector3D(static_cast<float>(angles[0]), static_cast<float>(angles[1]),
                              static_cast<float>(angles[2]));
    parsed.positionLine = posLine.trimmed();
    parsed.anglesLine = angLine.trimmed();
    parsed.confidence = 1.0;

    if (!velLine.isEmpty()) {
        QVector<double> velocity;
        if (parseNumberTokens(velLine, &velocity) && !velocity.isEmpty() &&
            std::abs(velocity.first()) <= 10000000.0) {
            parsed.speed = velocity.first();
            parsed.hasSpeed = true;
            parsed.velocityLine = velLine.trimmed();
        }
    }

    *sample = std::move(parsed);
    if (error) error->clear();
    return true;
}

} // namespace bspvision
