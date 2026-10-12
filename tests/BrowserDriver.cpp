#include "core/DownloadQueue/QueueManager.h"
#include "core/Runtime.h"
#include "browser/PipeServer.h"
#include "gui/MainWindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);if(argc!=2)return 2;downshift::CurlRuntime curl;downshift::QueueManager queue(QDir(QString::fromLocal8Bit(argv[1])).filePath("queue.sqlite"));
    auto s=queue.snapshot().get().settings;s.directory=QString::fromLocal8Bit(argv[1]);s.notifications=false;queue.configure(s).get();
    downshift::MainWindow window(queue);window.show();
    downshift::browser::PipeServer server(queue,[&window]{QMetaObject::invokeMethod(&window,[&window]{window.showNormal();window.raise();},Qt::QueuedConnection);});for(int i=0;i<100 && !server.active();++i)std::this_thread::sleep_for(std::chrono::milliseconds(20));if(!server.active())return 3;
    std::cout<<"ready"<<std::endl;
    std::thread commands([&]{std::string line;
    while(std::getline(std::cin,line)) {
        if(line=="exit")break;
        if(line.starts_with("remove "))queue.action(QString::fromStdString(line.substr(7)),downshift::QueueAction::Remove).get();
        QJsonArray rows;for(const auto& e:queue.snapshot().get().entries)rows.append(e.json());std::cout<<QJsonDocument(rows).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }QMetaObject::invokeMethod(&app,&QCoreApplication::quit,Qt::QueuedConnection);});
    const int result=app.exec();
    // Test parent closes stdin or issues exit before requesting GUI shutdown.
    commands.join();return result;
}
