#include "core/DownloadQueue/QueueManager.h"
#include "core/Runtime.h"
#include "browser/PipeServer.h"
#include "browser/NativeProtocol.h"
#include "TestWindows.h"
#include "gui/MainWindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDialog>
#include <QComboBox>
#include <QPixmap>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include <iostream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);if(argc!=2 && argc!=3)return 2;HiddenTestWindows hidden;
    if(argc==3){if(QString::fromLatin1(argv[2])!="--isolated-browser-test")return 2;downshift::browser::useIsolatedTestPipe();app.installEventFilter(&hidden);}
    downshift::CurlRuntime curl;downshift::QueueManager queue(QDir(QString::fromLocal8Bit(argv[1])).filePath("queue.sqlite"));
    auto s=queue.snapshot().get().settings;s.directory=QString::fromLocal8Bit(argv[1]);s.notifications=false;queue.configure(s).get();
    downshift::MainWindow window(queue);window.show();
    downshift::browser::PipeServer server(queue,[&window]{QMetaObject::invokeMethod(&window,[&window]{window.showNormal();window.raise();},Qt::QueuedConnection);},[&window](QString url,QString title){QMetaObject::invokeMethod(&window,[&window,url,title]{window.openVideo(url,title);},Qt::QueuedConnection);});for(int i=0;i<100 && !server.active();++i)std::this_thread::sleep_for(std::chrono::milliseconds(20));if(!server.active())return 3;
    std::cout<<"ready"<<std::endl;
    std::thread commands([&]{std::string line;
    while(std::getline(std::cin,line)) {
        if(line=="exit")break;
        if(line.starts_with("main-shot ")){QMetaObject::invokeMethod(&window,[&]{window.grab().save(QString::fromStdString(line.substr(10)),"BMP");},Qt::BlockingQueuedConnection);std::cout<<"captured"<<std::endl;continue;}
        if(line=="summary"){QJsonObject summary;QMetaObject::invokeMethod(&window,[&]{summary={{"ready",window.ready()},{"records",static_cast<int>(window.current().entries.size())},{"summary",window.findChild<QLabel*>("summary")->text()}};},Qt::BlockingQueuedConnection);std::cout<<QJsonDocument(summary).toJson(QJsonDocument::Compact).constData()<<std::endl;continue;}
        if(line=="confirm") {QMetaObject::invokeMethod(&window,[&]{for(auto* button:window.findChildren<QPushButton*>("captureStart"))if(button->isEnabled())button->click();},Qt::BlockingQueuedConnection);std::cout<<"confirmed"<<std::endl;continue;}
        if(line=="media" || line=="media-download" || line=="media-close" || line.starts_with("media-height ") || line.starts_with("media-shot ")) {
            QJsonArray media;QMetaObject::invokeMethod(&window,[&]{for(auto* dialog:window.findChildren<QDialog*>("mediaDialog")){
                auto* button=dialog->findChild<QPushButton*>("mediaDownload");auto* quality=dialog->findChild<QComboBox*>("mediaQuality");
                if(line.starts_with("media-height "))quality->setCurrentIndex(quality->findData(QString::fromStdString(line.substr(13)).toInt()));
                if(line.starts_with("media-shot "))dialog->grab().save(QString::fromStdString(line.substr(11)));
                if(line=="media-download" && button->isEnabled())button->click();
                QJsonArray qualities;for(int index=0;index<quality->count();++index)qualities.append(quality->itemText(index));
                media.append(QJsonObject{{"status",dialog->findChild<QLabel*>("mediaStatus")->text()},{"rate",dialog->findChild<QLabel*>("mediaRate")->text()},{"ready",button->isEnabled()},{"button",button->text()},{"qualities",qualities},{"selected",quality->currentText()}});
                if(line=="media-close")dialog->close();
            }},Qt::BlockingQueuedConnection);std::cout<<QJsonDocument(media).toJson(QJsonDocument::Compact).constData()<<std::endl;continue;
        }
        if(line.starts_with("remove "))queue.action(QString::fromStdString(line.substr(7)),downshift::QueueAction::Remove).get();
        if(line.starts_with("pause "))queue.action(QString::fromStdString(line.substr(6)),downshift::QueueAction::Pause).get();
        if(line.starts_with("resume "))queue.action(QString::fromStdString(line.substr(7)),downshift::QueueAction::Start).get();
        if(line.starts_with("cancel "))queue.action(QString::fromStdString(line.substr(7)),downshift::QueueAction::Cancel).get();
        if(line.starts_with("restart "))queue.action(QString::fromStdString(line.substr(8)),downshift::QueueAction::Restart).get();
        QJsonArray rows;for(const auto& e:queue.snapshot().get().entries)rows.append(e.json());std::cout<<QJsonDocument(rows).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }QMetaObject::invokeMethod(&app,&QCoreApplication::quit,Qt::QueuedConnection);});
    const int result=app.exec();
    // Test parent closes stdin or issues exit before requesting GUI shutdown.
    commands.join();return result;
}
