#include "../elinux/silica/core_process.h"
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <sys/wait.h>
#include <cstdio>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 const auto executable=QFileInfo("/bin/sleep").canonicalFilePath();
 CoreProcess process;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("MCONNECT_SILICA_CORE","1");process.setProcessEnvironment(environment);
 process.start("/bin/sleep",QStringList()<<"30");if(!process.waitForStarted())return 1;
 const auto identity=CoreIdentity::read(process.processId(),executable);if(!identity.matches())return 2;
 auto invalid=identity;invalid.birth+="invalid";StopOwnedCore(invalid);if(!identity.matches())return 3;
 StopOwnedCore(identity);process.waitForFinished(1000);if(identity.matches())return 4;
 int pipefd[2];if(pipe(pipefd))return 5;
 const pid_t parent=fork();
 if(parent==0){close(pipefd[0]);CoreProcess child;child.setProcessEnvironment(environment);child.start("/bin/sleep",QStringList()<<"30");if(!child.waitForStarted())::_exit(6);pid_t pid=child.processId();if(write(pipefd[1],&pid,sizeof(pid))!=ssize_t(sizeof(pid)))::_exit(7);::_exit(0);}
 close(pipefd[1]);pid_t child=0;if(read(pipefd[0],&child,sizeof(child))!=ssize_t(sizeof(child)))return 8;close(pipefd[0]);waitpid(parent,nullptr,0);
 QElapsedTimer timer;timer.start();while(CoreIdentity::read(child,executable).matches() && timer.elapsed()<3000)QThread::msleep(20);
 const auto orphan=CoreIdentity::read(child,executable);if(orphan.matches()){StopOwnedCore(orphan);return 9;}
 fprintf(stderr,"NATIVE_CORE_OWNER_PASS identity-guard parent-death-signal\n");return 0;
}
