#include "core/Runtime.h"
#include "gui/MainWindow.h"
#include "TestWindows.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
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
        if(argc==5&&QString::fromLocal8Bit(argv[4]).startsWith("--media-context=")){
            auto settings=window.current().settings;settings.automatic=false;settings.connections=8;settings.notifications=false;queue.configure(settings).get();
        }
        QJsonObject context;
        if(argc==5&&QString::fromLocal8Bit(argv[4]).startsWith("--media-context=")){
            QFile input(QString::fromLocal8Bit(argv[4]).mid(16));
            if(!input.open(QIODevice::ReadOnly)||input.size()>65536)throw std::runtime_error("Private media context is unreadable or too large");
            const auto document=QJsonDocument::fromJson(input.readAll());
            if(!document.isObject())throw std::runtime_error("Private media context must be an object");context=document.object();
        }else if(argc==5){
            if(QString::fromLatin1(argv[4])!="--unusable-browser")throw std::runtime_error("Unknown live acceptance option");
            // An unusable optional player descriptor must not override a
            // working public extractor or its full resolution/duration data.
            const auto id=QUrlQuery(QUrl(url)).queryItemValue("v");
            const QJsonArray formats{QJsonObject{{"url","http://127.0.0.1:1/unusable-browser.mp4"},{"format_id","18"},{"ext","mp4"},{"vcodec","unknown"},{"acodec","unknown"},{"filesize",1},{"height",360}}};
            context={{"youtube",QJsonObject{{"id",id},{"duration",2},{"formats",formats}}}};
        }
        window.openVideo(url,"Live media acceptance",QDir(root).filePath("videos"),context);
        auto* dialog=window.findChild<QDialog*>("mediaDialog");if(!dialog)throw std::runtime_error("Video dialog did not open");
        auto* submit=dialog->findChild<QPushButton*>("mediaDownload");auto* status=dialog->findChild<QLabel*>("mediaStatus");
        waitFor([&]{return submit->isEnabled();},110,"Live metadata did not finish");
        if(submit->text()!="Download video")throw std::runtime_error(status->text().toUtf8().constData());
        auto* quality=dialog->findChild<QComboBox*>("mediaQuality");QJsonArray heights;
        for(int i=0;i<quality->count();++i)if(quality->itemData(i).toInt()>0)heights.append(quality->itemData(i).toInt());
        const int choice=quality->findData(height);if(choice<0)throw std::runtime_error("Requested actual live resolution was not offered");quality->setCurrentIndex(choice);
        std::cerr<<"Actual native dialog resolved media qualities and selected "<<height<<"p. Starting full transfer.\n";
        submit->click();
        downshift::QueueEntry entry;
        waitFor([&]{
            const auto rows=queue.snapshot().get().entries;
            if(rows.size()!=1)return false;
            entry=rows.front();
            return entry.status==downshift::QueueStatus::Completed || entry.status==downshift::QueueStatus::Failed;
        },1800,"Live full transfer did not finish");
        if(entry.status!=downshift::QueueStatus::Completed)throw std::runtime_error(entry.error.toUtf8().constData());
        const QJsonObject report{{"completed",true},{"file",entry.destination},{"height",height},{"qualities",heights},{"expectedDuration",entry.videoMetadata.value("duration")},{"browserSource",entry.videoMetadata.value("browserSource")},{"privateQueue",true}};
        std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString()<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
