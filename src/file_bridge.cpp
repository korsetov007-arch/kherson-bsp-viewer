#include "file_bridge.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUuid>
FileBridge::FileBridge(QObject*p):QObject(p),sessionId_(QUuid::createUuid().toString(QUuid::WithoutBraces)){timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,[this]{if(QDateTime::currentSecsSinceEpoch()-lastHostWrite_>=1)writeHost();poll();});}
FileBridge::~FileBridge(){timer_.stop();if(configured()){QJsonObject h{{"protocol",1},{"session",sessionId_},{"heartbeat",QDateTime::currentSecsSinceEpoch()},{"app_running",false}};writeJsonAtomically(dataDirectory_+"/host.json",h);}}
void FileBridge::setGmodDirectory(const QString&root){dataDirectory_=QDir(root).filePath("data/tfc_suo");if(!QDir().mkpath(dataDirectory_)){lastError_="Cannot create garrysmod/data/tfc_suo";dataDirectory_.clear();return;}nextSequence_=0;state_={};lastError_.clear();if(!writeJsonAtomically(dataDirectory_+"/command.json",QJsonObject{{"protocol",1},{"session",sessionId_},{"seq",0},{"type","none"}}))return;writeHost();timer_.start();}
bool FileBridge::connected()const{if(!configured()||state_.value("session").toString()!=sessionId_)return false;qint64 hb=static_cast<qint64>(state_.value("heartbeat").toDouble(0));return hb>0&&QDateTime::currentSecsSinceEpoch()-hb<=3;}
bool FileBridge::writeJsonAtomically(const QString&path,const QJsonObject&o){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)){lastError_=f.errorString();return false;}QByteArray b=QJsonDocument(o).toJson(QJsonDocument::Compact);if(f.write(b)!=b.size()){lastError_=f.errorString();f.cancelWriting();return false;}if(!f.commit()){lastError_=f.errorString();return false;}return true;}
void FileBridge::writeHost(){if(!configured())return;QJsonObject h{{"protocol",1},{"session",sessionId_},{"heartbeat",QDateTime::currentSecsSinceEpoch()},{"app_running",true}};if(writeJsonAtomically(dataDirectory_+"/host.json",h))lastHostWrite_=QDateTime::currentSecsSinceEpoch();}
void FileBridge::poll(){if(!configured())return;QFile f(dataDirectory_+"/state.json");if(!f.open(QIODevice::ReadOnly))return;QJsonParseError e{};auto d=QJsonDocument::fromJson(f.readAll(),&e);if(e.error!=QJsonParseError::NoError||!d.isObject())return;auto s=d.object();if(s.value("protocol").toInt()!=1||s.value("session").toString()!=sessionId_||s==state_)return;state_=s;if(updatedCallback_)updatedCallback_();}
bool FileBridge::sendCommand(const QJsonObject&cmd){if(!configured()){lastError_="Garry's Mod directory is not configured.";return false;}QJsonObject e=cmd;e.insert("protocol",1);e.insert("session",sessionId_);e.insert("seq",static_cast<qint64>(++nextSequence_));e.insert("issued_at",QDateTime::currentSecsSinceEpoch());writeHost();return writeJsonAtomically(dataDirectory_+"/command.json",e);}
bool FileBridge::sendAim(double y,double p){return sendCommand(QJsonObject{{"type","aim"},{"world_yaw",y},{"view_pitch",p}});}
bool FileBridge::sendFire(double y,double p){return sendCommand(QJsonObject{{"type","fire"},{"world_yaw",y},{"view_pitch",p}});}
void FileBridge::sendCancel(){sendCommand(QJsonObject{{"type","cancel"}});}
