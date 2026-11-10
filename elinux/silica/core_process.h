#pragma once
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <unistd.h>
#include <sys/prctl.h>
#include <signal.h>

class CoreProcess : public QProcess {
 public:
  CoreProcess() : owner_(::getpid()) {}
 protected:
  void setupChildProcess() override {
    if(::getppid()!=owner_ || ::prctl(PR_SET_PDEATHSIG,SIGTERM)!=0 || ::getppid()!=owner_)::_exit(125);
  }
 private:
  pid_t owner_;
};

struct CoreIdentity {
  pid_t pid=0;
  QByteArray birth;
  QString executable;
  static CoreIdentity read(pid_t pid,const QString& expected) {
    const QString root=QString("/proc/%1/").arg(pid);
    if(pid<=0 || QFileInfo(root).ownerId()!=::getuid())return {};
    QString executable=QFileInfo(root+"exe").symLinkTarget();
    if(executable.endsWith(" (deleted)"))executable.chop(10);
    if(executable!=expected)return {};
    QFile environment(root+"environ"),stat(root+"stat");
    if(!environment.open(QIODevice::ReadOnly) || !stat.open(QIODevice::ReadOnly) || !environment.read(1024*1024).split('\0').contains("MCONNECT_SILICA_CORE=1"))return {};
    const auto bytes=stat.readAll();
    const auto fields=bytes.mid(bytes.lastIndexOf(')')+2).split(' ');
    if(fields.size()<=19 || fields[19].isEmpty())return {};
    return {pid,fields[19],expected};
  }
  bool matches() const {const auto current=read(pid,executable);return pid>0 && current.pid==pid && current.birth==birth;}
};

inline void StopOwnedCore(const CoreIdentity& identity) {
  if(!identity.matches())return;
  ::kill(identity.pid,SIGTERM);
  QElapsedTimer timer;timer.start();
  while(identity.matches() && timer.elapsed()<5000)QThread::msleep(20);
  if(identity.matches())::kill(identity.pid,SIGKILL);
}
