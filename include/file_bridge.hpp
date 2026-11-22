#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <utility>

class FileBridge final : public QObject {
public:
    explicit FileBridge(QObject *parent = nullptr);
    ~FileBridge() override;

    void setDeveloperOptions(const QString &muzzleAttachment,
                             double aimToleranceDeg,
                             double aimStepDegPerTick);
    void setGmodDirectory(const QString &garrysmodDirectory);

    bool configured() const { return !dataDirectory_.isEmpty(); }
    bool connected() const;
    QJsonObject state() const { return state_; }
    QString lastError() const { return lastError_; }

    bool sendAim(double worldYawDeg, double viewPitchDeg);
    bool sendFire(double worldYawDeg, double viewPitchDeg);
    void sendCancel();

    void setUpdatedCallback(std::function<void()> callback) {
        updatedCallback_ = std::move(callback);
    }

private:
    void poll();
    void writeHost();
    bool writeJsonAtomically(const QString &path, const QJsonObject &object);
    bool sendCommand(const QJsonObject &command);

    QString dataDirectory_;
    QString sessionId_;
    QString muzzleAttachment_ = QStringLiteral("auto");
    double aimToleranceDeg_ = 1.5;
    double aimStepDegPerTick_ = 2.0;
    QJsonObject state_;
    QString lastError_;
    quint64 nextSequence_ = 0;
    qint64 lastHostWrite_ = 0;
    QTimer timer_;
    std::function<void()> updatedCallback_;
};
