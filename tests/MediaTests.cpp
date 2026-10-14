#include "core/Runtime.h"
#include "gui/MainWindow.h"
#include "gui/MediaDialog.h"
#include "browser/NativeProtocol.h"
#include "database/QueueStore.h"
#include "TestWindows.h"
#include <QJsonDocument>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
template<class F> void wait(F condition,int timeout=90000){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);while(std::chrono::steady_clock::now()<end){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(condition())return;QThread::msleep(5);}throw std::runtime_error("Video test timed out");}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);QApplication::setApplicationName("Downshift");HiddenTestWindows hidden;app.installEventFilter(&hidden);if(argc!=4)return 2;
    const QString root=QString::fromLocal8Bit(argv[1]),base=QString::fromLatin1(argv[2]),shots=QString::fromLocal8Bit(argv[3]);
    try {
        downshift::CurlRuntime curl;QString interrupted;
        {downshift::QueueManager queue(QDir(root).filePath("queue.sqlite"));downshift::MainWindow window(queue);window.show();wait([&]{return window.ready();});
        auto settings=window.current().settings;settings.directory=root;auto configured=queue.configure(settings);wait([&]{return configured.wait_for(std::chrono::seconds(0))==std::future_status::ready;});configured.get();
        for(const auto& address:QStringList{"file:///C:/video.mp4","https://name:password@example.com/video.mp4","javascript:alert(1)"}) {bool rejected=false;try{downshift::browser::validate({{"version",1},{"command","media"},{"url",address},{"title","Video"}});}catch(...){rejected=true;}check(rejected,"Unsafe media address must be rejected");}
        downshift::browser::validate({{"version",1},{"command","media"},{"url",base+"/fixture.mp4"},{"title","Video"}});++checks;
        int beats=0;QTimer heartbeat;heartbeat.setInterval(20);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++beats;});heartbeat.start();
        int completedCount=0;
        for(const auto& file:QStringList{"fixture.mp4","stream.mpd","fixture.mp4"}) {
            window.openVideo(base+"/"+file,"Fixture video");auto* dialog=window.findChild<QDialog*>("mediaDialog");check(dialog!=nullptr,"Video dialog opens");
            window.openVideo(base+"/"+file,"Duplicate");check(window.findChildren<QDialog*>("mediaDialog").size()==1,"Same video reuses dialog");
            auto* submit=dialog->findChild<QPushButton*>("mediaDownload");auto* status=dialog->findChild<QLabel*>("mediaStatus");wait([&]{return submit->isEnabled();});
            check(submit->text()=="Download video",status->text().toUtf8().constData());check(dialog->findChild<QLineEdit*>("mediaDestination")->text()==QDir(root).filePath("Videos"),"Uses the video category folder");
            if(file=="stream.mpd")check(dialog->findChild<QComboBox*>("mediaQuality")->count()>1,"DASH qualities resolved");
            QDir().mkpath(shots);dialog->grab().save(QDir(shots).filePath(file+".bmp"),"BMP");submit->click();wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete" || submit->isEnabled();});
            check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",status->text().toUtf8().constData());check(status->text().startsWith("Saved:"),"Actual output filename displayed");
            ++completedCount;wait([&]{return std::count_if(window.current().entries.begin(),window.current().entries.end(),[](const auto& e){return e.media && e.status==downshift::QueueStatus::Completed;})==completedCount;});
            check(dialog->findChild<QPushButton*>("mediaOpenFolder")->isVisible(),"Completed video has an open-folder action");
            check(window.findChild<QTableWidget*>("downloadTable")->rowCount()==static_cast<int>(window.current().entries.size()),"Videos appear in main history table");
            const auto& record=window.current().entries.back();check(QFile::exists(record.destination) && record.progress.totalBytes.value_or(0)>0,"History records the actual saved file and size");
            dialog->close();wait([&]{return window.findChild<QDialog*>("mediaDialog")==nullptr;});
        }
        check(beats>10,"GUI heartbeat runs during media work");
        // Failed site resolution gives a retry, without claiming a successful download.
        window.openVideo(base+"/collection.html","Collection");auto* collection=window.findChild<QDialog*>("mediaDialog");wait([&]{return collection->findChild<QPushButton*>("mediaDownload")->isEnabled();});
        check(collection->findChild<QPushButton*>("mediaDownload")->text()=="Retry check","A playlist cannot masquerade as a best-quality video");
        check(collection->findChild<QComboBox*>("mediaQuality")->count()==0,"A playlist offers no fictitious qualities");collection->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        bool webpageRejected=false;try{queue.add("https://www.youtube.com/watch?v=HyfC9nZvIJM",QDir(root).filePath("watch"),0,false).get();}catch(...){webpageRejected=true;}check(webpageRejected,"YouTube watch pages never enter the ordinary file queue");
        window.openVideo(base+"/missing.mp4","Missing");auto* failed=window.findChild<QDialog*>("mediaDialog");auto* retry=failed->findChild<QPushButton*>("mediaDownload");wait([&]{return retry->isEnabled();});check(retry->text()=="Retry check","Unsupported/error is explicit");failed->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        // yt-dlp may exit successfully when --no-overwrites skips an existing output.
        // An invalid pre-existing file must fail verification, never claim completion.
        window.openVideo(base+"/broken.mp4","Broken");auto* broken=window.findChild<QDialog*>("mediaDialog");auto* submit=broken->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video","Broken fixture metadata resolved");submit->click();wait([&]{return submit->isEnabled();});
        check(broken->findChild<QLabel*>("mediaRate")->text()!="Complete","Zero-exit helper cannot complete invalid output");
        wait([&]{return window.current().entries.size()==4 && window.current().entries.back().status==downshift::QueueStatus::Failed;});check(!window.current().entries.back().error.isEmpty(),"Failure persisted with explanation");broken->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        const auto customFolder=QDir(root).filePath("chosen folder");QDir().mkpath(customFolder);
        window.openVideo(base+"/fixture.mp4","Cancel fixture",customFolder);auto* stopped=window.findChild<QDialog*>("mediaDialog");auto* start=stopped->findChild<QPushButton*>("mediaDownload");wait([&]{return start->isEnabled();});start->click();auto* stop=stopped->findChild<QPushButton*>("mediaCancel");wait([&]{return stop->isEnabled() && stop->text()=="Cancel";});stop->click();wait([&]{return start->isEnabled();});
        wait([&]{return window.current().entries.size()==5 && window.current().entries.back().status==downshift::QueueStatus::Cancelled;});check(stopped->findChild<QLabel*>("mediaRate")->text()!="Complete","Stopped helper is not complete");stopped->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto* table=window.findChild<QTableWidget*>("downloadTable");const auto retryId=window.current().entries.back().id;int retryRow=-1;for(int row=0;row<table->rowCount();++row)if(table->item(row,0)->data(Qt::UserRole).toString()==retryId)retryRow=row;check(retryRow>=0,"Sorted table retains the cancelled video's ID");table->selectRow(retryRow);window.findChild<QPushButton*>("startButton")->click();auto* retryVideo=window.findChild<QDialog*>("mediaDialog");check(retryVideo!=nullptr,"Main-list retry opens video window");wait([&]{return retryVideo->findChild<QPushButton*>("mediaDownload")->isEnabled();});check(retryVideo->findChild<QLineEdit*>("mediaDestination")->text()==customFolder,"Retry preserves selected video folder");retryVideo->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto missing=queue.beginMedia(base+"/absent.mp4",root,"Missing output").get();bool rejected=false;try{queue.finishMedia(missing,downshift::QueueStatus::Completed,QDir(root).filePath("absent.mp4"),{}).get();}catch(...){rejected=true;}check(rejected,"No completion for missing output");queue.finishMedia(missing,downshift::QueueStatus::Failed,{},"No final output").get();
        auto cancelled=queue.beginMedia(base+"/cancel.mp4",root,"Cancelled video").get();queue.finishMedia(cancelled,downshift::QueueStatus::Cancelled,{},"Stopped").get();
        bool nativeRejected=false;try{queue.action(cancelled,downshift::QueueAction::Start).get();}catch(...){nativeRejected=true;}check(nativeRejected,"Native file worker cannot start media page URLs");
        interrupted=queue.beginMedia(base+"/interrupt.mp4",root,"Interrupted video").get();
        }
        {downshift::QueueManager reopened(QDir(root).filePath("queue.sqlite"));const auto history=reopened.snapshot().get();check(history.entries.size()==8,"All video history survives reopening");check(std::count_if(history.entries.begin(),history.entries.end(),[](const auto& e){return e.status==downshift::QueueStatus::Completed;})==3,"Completed videos survive reopening");check(history.entries.back().id==interrupted && history.entries.back().status==downshift::QueueStatus::Failed,"Interrupted video is held safely for retry after reopening");}
        const auto legacyDb=QDir(root).filePath("legacy.sqlite"),legacyFile=QDir(root).filePath("watch");
        {QFile file(legacyFile);check(file.open(QIODevice::WriteOnly),"Legacy page fixture opens");file.write("<html>YouTube watch page</html>");}
        downshift::QueueEntry old;old.id=QString(32,'a');old.url="https://www.youtube.com/watch?v=HyfC9nZvIJM";old.destination=legacyFile;old.status=downshift::QueueStatus::Completed;
        {downshift::QueueStore store(legacyDb);store.save(old.id,QJsonDocument(old.json()).toJson(QJsonDocument::Compact));}
        {downshift::QueueManager queue(legacyDb);const auto snapshot=queue.snapshot().get();check(snapshot.entries[0].status==downshift::QueueStatus::Failed,"Legacy watch-page completion is corrected");check(QFile::exists(legacyFile),"Legacy file is preserved");
        bool prevented=false;try{queue.action(old.id,downshift::QueueAction::Start).get();}catch(...){prevented=true;}check(prevented,"Legacy watch page cannot restart as an ordinary file");}
        {const auto folder=QDir(root).filePath("quality copies");QDir().mkpath(folder);downshift::QueueManager queue(QDir(root).filePath("qualities.sqlite"));downshift::MainWindow window(queue);window.show();QString first;qint64 firstSize=0;
        for(const auto height:{120,240}) {
            window.openVideo(base+"/stream.mpd","Quality fixture",folder);auto* dialog=window.findChild<QDialog*>("mediaDialog");auto* submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video","Quality fixture resolves");
            auto* quality=dialog->findChild<QComboBox*>("mediaQuality");const auto index=quality->findData(height);check(index>=0,"Specific resolution is offered");quality->setCurrentIndex(index);submit->click();
            wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete" || submit->isEnabled();});check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());
            const auto record=queue.snapshot().get().entries.back();check(record.destination.contains("["+QString::number(height)+"p]"),"Actual resolution appears in the final filename");
            if(first.isEmpty()){first=record.destination;firstSize=QFileInfo(first).size();}else {check(first!=record.destination,"A different quality has its own output");check(QFileInfo(first).size()==firstSize,"Higher quality preserves the earlier file");}
            dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }}
        std::cout<<"Media GUI checks passed: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
