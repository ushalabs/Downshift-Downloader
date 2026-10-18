#include "core/Runtime.h"
#include "gui/MainWindow.h"
#include "TestWindows.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QUrlQuery>
#include <iostream>
#include <stdexcept>

template<class F> void waitFor(F ready,int seconds,const char* phase){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    while(!ready()){
        if(std::chrono::steady_clock::now()>end)throw std::runtime_error(phase);
        QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(10);
    }
}
int main(int argc,char** argv){
    QApplication app(argc,argv);QApplication::setApplicationName("Downshift private live acceptance");HiddenTestWindows hidden;app.installEventFilter(&hidden);
    if(argc!=4&&argc!=5)return 2;
    try{
        downshift::CurlRuntime curl;const QString root=QString::fromLocal8Bit(argv[1]),url=QString::fromLatin1(argv[2]);const int height=QString::fromLatin1(argv[3]).toInt();
        if(!QDir().mkpath(QDir(root).filePath("videos")))throw std::runtime_error("Private video folder could not be created");
        downshift::QueueManager queue(QDir(root).filePath("private-queue.sqlite"));downshift::MainWindow window(queue);window.show();
        waitFor([&]{return window.ready();},30,"Private queue did not load");
        QJsonObject context;
        if(argc==5){
            // An unusable optional player descriptor must not override a
            // working public extractor or its full resolution/duration data.
            const auto id=QUrlQuery(QUrl(url)).queryItemValue("v");
            const QJsonArray formats{QJsonObject{{"url","http://127.0.0.1:1/unusable-browser.mp4"},{"format_id","18"},{"ext","mp4"},{"vcodec","unknown"},{"acodec","unknown"},{"filesize",1},{"height",360}}};
            context={{"youtube",QJsonObject{{"id",id},{"duration",2},{"formats",formats}}}};
        }
        window.openVideo(url,"Live YouTube acceptance",QDir(root).filePath("videos"),context);
        auto* dialog=window.findChild<QDialog*>("mediaDialog");if(!dialog)throw std::runtime_error("Video dialog did not open");
        auto* submit=dialog->findChild<QPushButton*>("mediaDownload");auto* status=dialog->findChild<QLabel*>("mediaStatus");
        waitFor([&]{return submit->isEnabled();},110,"Live metadata did not finish");
        if(submit->text()!="Download video")throw std::runtime_error(status->text().toUtf8().constData());
        auto* quality=dialog->findChild<QComboBox*>("mediaQuality");QJsonArray heights;
        for(int i=0;i<quality->count();++i)if(quality->itemData(i).toInt()>0)heights.append(quality->itemData(i).toInt());
        const int choice=quality->findData(height);if(choice<0)throw std::runtime_error("Requested actual live resolution was not offered");quality->setCurrentIndex(choice);
        std::cerr<<"Actual native dialog resolved YouTube qualities and selected "<<height<<"p. Starting full transfer.\n";
        submit->click();
        waitFor([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete"||submit->isEnabled();},900,"Live full transfer did not finish");
        if(dialog->findChild<QLabel*>("mediaRate")->text()!="Complete")throw std::runtime_error(status->text().toUtf8().constData());
        const auto snapshot=queue.snapshot().get();if(snapshot.entries.size()!=1||snapshot.entries[0].status!=downshift::QueueStatus::Completed)throw std::runtime_error("Live transfer was not durably completed");
        const auto& entry=snapshot.entries[0];
        const QJsonObject report{{"completed",true},{"file",entry.destination},{"height",height},{"qualities",heights},{"expectedDuration",entry.videoMetadata.value("duration")},{"browserSource",entry.videoMetadata.value("browserSource")},{"privateQueue",true}};
        std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString()<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
