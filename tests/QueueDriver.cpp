#include "core/DownloadQueue/QueueManager.h"
#include "core/Runtime.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); QCoreApplication::setApplicationName("Downshift");
    if(argc!=2) return 2;
    downshift::CurlRuntime curl; downshift::QueueManager queue(QString::fromLocal8Bit(argv[1]));
    std::string line;
    while(std::getline(std::cin,line)) {
        QJsonObject response;
        try {
            const auto command=QJsonDocument::fromJson(QByteArray::fromStdString(line)).object(); const auto op=command.value("op").toString();
            if(op=="exit") break;
            if(op=="add") response["id"]=queue.add(command.value("url").toString(),command.value("destination").toString(),command.value("priority").toInt(),command.value("start").toBool(true),command.value("sha256").toString(),command.value("speedLimit").toString().toULongLong()).get();
            else if(op=="action") queue.action(command.value("id").toString(),static_cast<downshift::QueueAction>(command.value("action").toInt())).get();
            else if(op=="rename") queue.rename(command.value("id").toString(),command.value("destination").toString()).get();
            else if(op=="configure") queue.configure(downshift::QueueSettings::fromJson(command.value("settings").toObject())).get();
            else if(op=="snapshot") {
                const auto snapshot=queue.snapshot().get(); QJsonArray rows;
                for(const auto& e:snapshot.entries) { auto row=e.json(); row["speed"]=e.progress.smoothedBytesPerSecond; row["active"]=static_cast<int>(e.progress.activeRanges); rows.append(row); }
                response["entries"]=rows; response["settings"]=snapshot.settings.json();
            } else throw std::runtime_error("Unknown command.");
            response["ok"]=true;
        } catch(const std::exception& e) { response["ok"]=false; response["error"]=QString::fromUtf8(e.what()); }
        std::cout<<QJsonDocument(response).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }
}
