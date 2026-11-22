#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
class FileBridge final:public QObject{
public: explicit FileBridge(QObject* p=nullptr);~FileBridge() override;void setGmodDirectory(const QString&);bool configured()const{return !dataDirectory_.isEmpty();}bool connected()const;QJsonObject state()const{return state_;}QString lastError()const{return lastError_;}bool sendAim(double,double);bool sendFire(double,double);void sendCancel();void setUpdatedCallback(std::function<void()> cb){updatedCallback_=std::move(cb);}
private:void poll();void writeHost();bool writeJsonAtomically(const QString&,const QJsonObject&);bool sendCommand(const QJsonObject&);
QString dataDirectory_,sessionId_;QJsonObject state_;QString lastError_;quint64 nextSequence_=0;qint64 lastHostWrite_=0;QTimer timer_;std::function<void()> updatedCallback_;
};
