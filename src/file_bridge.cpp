#include "file_bridge.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>

FileBridge::FileBridge(QObject *parent)
    : QObject(parent),
      sessionId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (QDateTime::currentSecsSinceEpoch() - lastHostWrite_ >= 1)
            writeHost();
        poll();
    });
}

FileBridge::~FileBridge()
{
    timer_.stop();
    if (configured()) {
        QJsonObject host{
            {"protocol", 1},
            {"session", sessionId_},
            {"heartbeat", QDateTime::currentSecsSinceEpoch()},
            {"app_running", false}
        };
        writeJsonAtomically(dataDirectory_ + "/host.json", host);
    }
}

void FileBridge::setDeveloperOptions(const QString &muzzleAttachment,
                                     double aimToleranceDeg,
                                     double aimStepDegPerTick)
{
    muzzleAttachment_ = muzzleAttachment.trimmed().isEmpty()
        ? QStringLiteral("auto") : muzzleAttachment.trimmed();
    aimToleranceDeg_ = qBound(0.1, aimToleranceDeg, 10.0);
    aimStepDegPerTick_ = qBound(0.1, aimStepDegPerTick, 10.0);
    writeHost();
}

void FileBridge::setGmodDirectory(const QString &garrysmodDirectory)
{
    dataDirectory_ = QDir(garrysmodDirectory).filePath("data/tfc_suo");
    if (!QDir().mkpath(dataDirectory_)) {
        lastError_ = "Cannot create garrysmod/data/tfc_suo";
        dataDirectory_.clear();
        return;
    }

    nextSequence_ = 0;
    state_ = {};
    lastError_.clear();

    if (!writeJsonAtomically(
            dataDirectory_ + "/command.json",
            QJsonObject{{"protocol", 1}, {"session", sessionId_},
                        {"seq", 0}, {"type", "none"}}))
        return;

    writeHost();
    timer_.start();
}

bool FileBridge::connected() const
{
    if (!configured() || state_.value("session").toString() != sessionId_)
        return false;

    const qint64 heartbeat =
        static_cast<qint64>(state_.value("heartbeat").toDouble(0.0));
    return heartbeat > 0 &&
           QDateTime::currentSecsSinceEpoch() - heartbeat <= 3;
}

bool FileBridge::writeJsonAtomically(const QString &path,
                                     const QJsonObject &object)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        lastError_ = file.errorString();
        return false;
    }

    const QByteArray bytes =
        QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) {
        lastError_ = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        lastError_ = file.errorString();
        return false;
    }
    return true;
}

void FileBridge::writeHost()
{
    if (!configured())
        return;

    QJsonObject host{
        {"protocol", 1},
        {"session", sessionId_},
        {"heartbeat", QDateTime::currentSecsSinceEpoch()},
        {"app_running", true},
        {"muzzle_attachment", muzzleAttachment_},
        {"aim_tolerance_deg", aimToleranceDeg_},
        {"aim_step_deg_per_tick", aimStepDegPerTick_}
    };

    if (writeJsonAtomically(dataDirectory_ + "/host.json", host))
        lastHostWrite_ = QDateTime::currentSecsSinceEpoch();
}

void FileBridge::poll()
{
    if (!configured())
        return;

    QFile file(dataDirectory_ + "/state.json");
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject candidate = document.object();
    if (candidate.value("protocol").toInt() != 1 ||
        candidate.value("session").toString() != sessionId_ ||
        candidate == state_)
        return;

    state_ = candidate;
    if (updatedCallback_)
        updatedCallback_();
}

bool FileBridge::sendCommand(const QJsonObject &command)
{
    if (!configured()) {
        lastError_ = "Garry's Mod directory is not configured.";
        return false;
    }

    QJsonObject envelope = command;
    envelope.insert("protocol", 1);
    envelope.insert("session", sessionId_);
    envelope.insert("seq", static_cast<qint64>(++nextSequence_));
    envelope.insert("issued_at", QDateTime::currentSecsSinceEpoch());

    writeHost();
    return writeJsonAtomically(dataDirectory_ + "/command.json", envelope);
}

bool FileBridge::sendAim(double worldYawDeg, double viewPitchDeg,
                          double muzzleYawDeg, double muzzlePitchDeg)
{
    return sendCommand(QJsonObject{
        {"type", "aim"},
        {"world_yaw", worldYawDeg},
        {"view_pitch", viewPitchDeg},
        {"muzzle_yaw", muzzleYawDeg},
        {"muzzle_pitch", muzzlePitchDeg}
    });
}

bool FileBridge::sendFire(double worldYawDeg, double viewPitchDeg,
                           double muzzleYawDeg, double muzzlePitchDeg)
{
    return sendCommand(QJsonObject{
        {"type", "fire"},
        {"world_yaw", worldYawDeg},
        {"view_pitch", viewPitchDeg},
        {"muzzle_yaw", muzzleYawDeg},
        {"muzzle_pitch", muzzlePitchDeg}
    });
}

void FileBridge::sendCancel()
{
    sendCommand(QJsonObject{{"type", "cancel"}});
}
