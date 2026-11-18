#include "../plugins/Base_Output/Output.h"
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <QApplication>
#include <QAction>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QMessageBox>
#include <QLoggingCategory>
#include <QProcess>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <thread>
#include <iostream>
Q_LOGGING_CATEGORY(testSolverLog,"cae.solver")

class LogFrame : public UiFrameService {
public:
 QWidget* log=nullptr;
 QAction* addWidget(QWidget* w,QString) override {log=w;return new QAction(w);}
 QAction* addWidgetForTabwidget(QWidget* w,QString s) override{return addWidget(w,s);}
 void setWidgetVisible(QWidget*,bool) override{}
 void showWidgetInLogArea(QWidget*,QString) override{}
};

int main(int argc,char** argv)
{
 if(argc>1&&QString::fromLocal8Bit(argv[1])=="--stream-child")
 {std::cout<<"STREAM_FIRST\n"<<std::flush;QThread::msleep(700);std::cerr<<"STREAM_SECOND\n"<<std::flush;QThread::msleep(700);return 0;}
 QApplication app(argc,argv);QTemporaryDir root;LogFrame frame;
 ServiceRegistry::instance().registerService<UiFrameService>(&frame);
 PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey,root.path(),"project_dir");
 {
  Output output;Output::installQtLogBridge(); // A null previous handler must not cause reinstallation/recursion.
  qInfo().noquote()<<"__RAW__solver stdout: ITER=2 ERR=0.013\n  <native> &  1.0";
  std::thread worker([]{qCWarning(testSolverLog).noquote()<<QStringLiteral("准备失败：D:/IIIDIR.DAT 无法写入");});worker.join();
  qInfo().noquote()<<"__RAW__solver stderr: native error";
  qWarning()<<"unrelated-Qt-warning";
  qCritical()<<"unrelated-critical-error";
  qDebug()<<"internal-debug-not-for-users";
  QMessageBox warning(QMessageBox::Warning,QStringLiteral("计算程序未启动"),QStringLiteral("early preparation error"));
  warning.setDetailedText("required-file.dat");warning.show();
  app.processEvents();
  auto* text=frame.log->findChild<QTextEdit*>();if(!text)return 1;
  const QString visible=text->toPlainText();
  if(!visible.contains("\n  <native> &  1.0"))return 5;
  if(!visible.contains("ITER=2")||!visible.contains("D:/IIIDIR.DAT")||!visible.contains("native error")||visible.contains("required-file.dat")||visible.contains("internal-debug-not-for-users")||visible.contains("unrelated-Qt-warning")||visible.contains("unrelated-critical-error"))return 2;
  QFile file(QDir(root.path()).filePath(QStringLiteral("日志/运行日志_%1.log").arg(QDate::currentDate().toString("yyyyMMdd"))));
  if(!file.open(QIODevice::ReadOnly))return 3;
  const auto stored=QString::fromUtf8(file.readAll());
  if(!stored.contains("ITER=2")||!stored.contains(QStringLiteral("无法写入"))||!stored.contains("native error"))return 4;
  if(stored.contains("unrelated-Qt-warning")||stored.contains("required-file.dat"))return 6;
  QProcess child;child.setProcessChannelMode(QProcess::MergedChannels);QEventLoop loop;bool visibleBeforeExit=false;
  QObject::connect(&child,&QProcess::readyReadStandardOutput,[&]{
   qInfo().noquote()<<"__RAW__"+QString::fromUtf8(child.readAllStandardOutput());
   if(child.state()==QProcess::Running&&text->toPlainText().contains("STREAM_FIRST"))visibleBeforeExit=true;
  });
  QObject::connect(&child,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),&loop,&QEventLoop::quit);
  QTimer::singleShot(5000,&loop,&QEventLoop::quit);
  child.start(QCoreApplication::applicationFilePath(),{"--stream-child"});loop.exec();
  if(!visibleBeforeExit||!text->toPlainText().contains("STREAM_SECOND"))return 7;
 }
 delete frame.log;
 std::cout<<"PASS: solver output and preparation errors reach UI and UTF-8 file, including worker warnings\n";
}
