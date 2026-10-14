#include "core/DownloadQueue/StorageLocation.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temporary;if(!temporary.isValid())return 1;
    int checks=0;const QString sid="S-1-5-21-123-456-789-1001",folder=temporary.path(),binding=QDir(folder).filePath("downshift-storage.json");
    const auto require=[&](bool value){if(!value)throw std::runtime_error("Storage location regression");++checks;};
    const auto write=[&](QByteArray value){QFile f(binding);require(f.open(QIODevice::WriteOnly));require(f.write(value)==value.size());};
    const auto rejected=[&]{try{(void)downshift::storageDirectory(folder,sid);}catch(const std::exception&){++checks;return;}throw std::runtime_error("Invalid storage binding was accepted");};
    try{
        require(downshift::storageDirectory(folder,sid).endsWith("Usha Labs/Downshift"));
        write("{\"version\":1,\"dataDirectory\":\"state\"}");
        const auto expected=QDir(folder).filePath("state/users/"+sid);require(downshift::storageDirectory(folder,sid)==expected);
        require(QDir().mkpath(expected));QFile db(QDir(expected).filePath("queue.sqlite"));require(db.open(QIODevice::WriteOnly));require(db.write("test")==4);db.close();
        const auto identity=downshift::storageIdentity(db.fileName());require(identity.size()==32);
        write(QJsonDocument(QJsonObject{{"version",1},{"dataDirectory",QDir(folder).filePath("state")},{"expectedIdentity",identity}}).toJson());require(downshift::storageDirectory(folder,sid)==expected);
        write("{\"version\":1,\"dataDirectory\":\"state\",\"expectedIdentity\":\"00000000000000000000000000000000\"}");rejected();
        write("{broken");rejected();write("{\"version\":2,\"dataDirectory\":\"state\"}");rejected();write("{\"version\":1}");rejected();
        write(QByteArray(8193,'x'));rejected();write("{\"version\":1,\"dataDirectory\":\"state\"}");
        try{(void)downshift::storageDirectory(folder,"../other-user");throw std::runtime_error("SID path accepted");}catch(const std::runtime_error& e){require(QString::fromUtf8(e.what())!="SID path accepted");}
        std::cout<<"Storage binding checks passed: "<<checks<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
