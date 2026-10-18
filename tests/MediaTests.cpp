#include "core/Runtime.h"
#include "gui/MainWindow.h"
#include "gui/MediaDialog.h"
#include "browser/NativeProtocol.h"
#include "database/QueueStore.h"
#include "TestWindows.h"
#include <QJsonDocument>
#include <QJsonArray>
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
#include <QAction>
#include <QCheckBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QMap>
#include <iostream>
#include <stdexcept>
#include <source_location>
namespace {
int checks=0;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;std::cerr<<"Media check "<<checks<<": "<<message<<'\n';}
template<class F> void wait(F condition,int timeout=90000,const std::source_location location=std::source_location::current()){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);while(std::chrono::steady_clock::now()<end){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(condition())return;QThread::msleep(5);}throw std::runtime_error("Video test timed out at line "+std::to_string(location.line()));}
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
            else{auto* quality=dialog->findChild<QComboBox*>("mediaQuality");check(quality->count()==1 && !quality->isEnabled(),"Single quality uses default without requiring selection");}
            QDir().mkpath(shots);dialog->grab().save(QDir(shots).filePath(file+".bmp"),"BMP");submit->click();wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete" || submit->isEnabled();});
            check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",status->text().toUtf8().constData());check(status->text().startsWith("Saved:"),"Actual output filename displayed");
            ++completedCount;wait([&]{return std::count_if(window.current().entries.begin(),window.current().entries.end(),[](const auto& e){return e.media && e.status==downshift::QueueStatus::Completed;})==completedCount;});
            check(dialog->findChild<QPushButton*>("mediaOpenFolder")->isVisible(),"Completed video has an open-folder action");
            check(window.findChild<QTableWidget*>("downloadTable")->rowCount()==static_cast<int>(window.current().entries.size()),"Videos appear in main history table");
            const auto& record=window.current().entries.back();check(QFile::exists(record.destination) && record.progress.totalBytes.value_or(0)>0,"History records the actual saved file and size");
            if(completedCount==1){auto* list=window.findChild<QTableWidget*>("downloadTable");for(int row=0;row<list->rowCount();++row)if(list->item(row,0)->data(Qt::UserRole).toString()==record.id)list->selectRow(row);wait([&]{return window.findChild<QLabel*>("previewFallback")->toolTip()=="Local video preview";},15000);check(!window.findChild<QLabel*>("previewFallback")->pixmap().isNull(),"Completed video supplies a real first-frame preview");}
            dialog->close();wait([&]{return window.findChild<QDialog*>("mediaDialog")==nullptr;});
        }
        check(beats>10,"GUI heartbeat runs during media work");
        // Long recordings must not fail quality lookup because of unused
        // fragment metadata. Repeat in the same dialog to exercise retry state.
        window.openVideo(base+"/large.mpd","Large metadata");auto* large=window.findChild<QDialog*>("mediaDialog");auto* largeSubmit=large->findChild<QPushButton*>("mediaDownload");
        for(int attempt=0;attempt<2;++attempt){wait([&]{return largeSubmit->isEnabled();});check(largeSubmit->text()=="Download video",large->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());const auto* quality=large->findChild<QComboBox*>("mediaQuality");check(quality->count()==1 && quality->currentData().toInt()==240 && quality->currentText()=="240p","Long-video single known resolution is labeled explicitly");if(attempt==0)dynamic_cast<downshift::MediaDialog*>(large)->inspect();}
        check(queue.snapshot().get().entries.size()==3,"Quality inspection never creates a download history row");large->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        // Failed site resolution gives a retry, without claiming a successful download.
        window.openVideo(base+"/collection.html","Collection");auto* collection=window.findChild<QDialog*>("mediaDialog");wait([&]{return collection->findChild<QPushButton*>("mediaDownload")->isEnabled();});
        check(collection->findChild<QPushButton*>("mediaDownload")->text()=="Retry check","A playlist cannot masquerade as a best-quality video");
        check(collection->findChild<QComboBox*>("mediaQuality")->count()==0,"A playlist offers no fictitious qualities");collection->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        bool webpageRejected=false;try{queue.add("https://www.youtube.com/watch?v=HyfC9nZvIJM",QDir(root).filePath("watch"),0,false).get();}catch(...){webpageRejected=true;}check(webpageRejected,"YouTube watch pages never enter the ordinary file queue");
        window.openVideo(base+"/missing.mp4","Missing");auto* failed=window.findChild<QDialog*>("mediaDialog");auto* retry=failed->findChild<QPushButton*>("mediaDownload");wait([&]{return retry->isEnabled();});check(retry->text()=="Retry check","Unsupported/error is explicit");failed->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        // Resolve successfully, then fail the actual transfer. Unrelated old
        // files in the destination must never be reused or removed by staging.
        window.openVideo(base+"/broken.mp4","Broken");auto* broken=window.findChild<QDialog*>("mediaDialog");auto* submit=broken->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video","Broken fixture metadata resolved");{QFile marker(QDir(root).filePath("../source/.fail-broken"));check(marker.open(QIODevice::WriteOnly),"Arm private transfer failure");}submit->click();wait([&]{return submit->isEnabled();});
        check(broken->findChild<QLabel*>("mediaRate")->text()!="Complete","Zero-exit helper cannot complete invalid output");
        wait([&]{return window.current().entries.size()==4 && window.current().entries.back().status==downshift::QueueStatus::Failed;});check(!window.current().entries.back().error.isEmpty(),"Failure persisted with explanation");check(!QFileInfo::exists(QDir(window.current().entries.back().mediaFolder).filePath(".downshift-video-"+window.current().entries.back().id)),"Failed video staging is removed");broken->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        const auto customFolder=QDir(root).filePath("chosen folder");QDir().mkpath(customFolder);
        window.openVideo(base+"/fixture.mp4","Cancel fixture",customFolder);auto* stopped=window.findChild<QDialog*>("mediaDialog");auto* start=stopped->findChild<QPushButton*>("mediaDownload");wait([&]{return start->isEnabled();});start->click();auto* stop=stopped->findChild<QPushButton*>("mediaCancel");wait([&]{return stop->isEnabled() && stop->text()=="Cancel";});stop->click();wait([&]{return start->isEnabled();});
        wait([&]{return window.current().entries.size()==5 && window.current().entries.back().status==downshift::QueueStatus::Cancelled;});check(stopped->findChild<QLabel*>("mediaRate")->text()!="Complete","Stopped helper is not complete");check(!QFileInfo::exists(QDir(customFolder).filePath(".downshift-video-"+window.current().entries.back().id)),"Cancelled video staging is removed");stopped->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto* table=window.findChild<QTableWidget*>("downloadTable");const auto retryId=window.current().entries.back().id;int retryRow=-1;for(int row=0;row<table->rowCount();++row)if(table->item(row,0)->data(Qt::UserRole).toString()==retryId)retryRow=row;check(retryRow>=0,"Sorted table retains the cancelled video's ID");table->selectRow(retryRow);window.findChild<QPushButton*>("startButton")->click();auto* retryVideo=window.findChild<QDialog*>("mediaDialog");check(retryVideo!=nullptr,"Main-list retry opens video window");wait([&]{return retryVideo->findChild<QPushButton*>("mediaDownload")->isEnabled();});check(retryVideo->findChild<QLineEdit*>("mediaDestination")->text()==customFolder,"Retry preserves selected video folder");retryVideo->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto missing=queue.beginMedia(base+"/absent.mp4",root,"Missing output").get();bool rejected=false;try{queue.finishMedia(missing,downshift::QueueStatus::Completed,QDir(root).filePath("absent.mp4"),{}).get();}catch(...){rejected=true;}check(rejected,"No completion for missing output");queue.finishMedia(missing,downshift::QueueStatus::Failed,{},"No final output").get();
        auto cancelled=queue.beginMedia(base+"/cancel.mp4",root,"Cancelled video").get();queue.finishMedia(cancelled,downshift::QueueStatus::Cancelled,{},"Stopped").get();
        bool nativeRejected=false;try{queue.action(cancelled,downshift::QueueAction::Start).get();}catch(...){nativeRejected=true;}check(nativeRejected,"Native file worker cannot start media page URLs");
        interrupted=queue.beginMedia(base+"/interrupt.mp4",root,"Interrupted video").get();QFile fragment(QDir(root).filePath(".downshift-video-"+interrupted+"/fragment.part"));check(fragment.open(QIODevice::WriteOnly),"Interrupted fragment fixture");fragment.write("incomplete");fragment.close();
        }
        {downshift::QueueManager reopened(QDir(root).filePath("queue.sqlite"));const auto history=reopened.snapshot().get();check(!QFileInfo::exists(QDir(root).filePath(".downshift-video-"+interrupted)),"Restart cleans interrupted video fragments");check(history.entries.size()==8,"All video history survives reopening");check(std::count_if(history.entries.begin(),history.entries.end(),[](const auto& e){return e.status==downshift::QueueStatus::Completed;})==3,"Completed videos survive reopening");check(history.entries.back().id==interrupted && history.entries.back().status==downshift::QueueStatus::Failed,"Interrupted video is held safely for retry after reopening");}
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
        {const auto folder=QDir(root).filePath("default hls");QDir().mkpath(folder);downshift::QueueManager queue(QDir(root).filePath("hls.sqlite"));downshift::MainWindow window(queue);window.show();
        const QJsonObject context{{"detectedStream",true},{"referer",base+"/embed/player"},{"origin","https://player.fixture.test"},{"userAgent","DownshiftMediaFixture/1.0"}};
        downshift::browser::validate({{"version",1},{"command","media"},{"url",base+"/hls-default.txt"},{"title","Default stream"},{"context",context}});++checks;
        for(const auto bad:QJsonObject{{"referer","https://player.test/\r\nCookie: forbidden"},{"cookie","forbidden"},{"userAgent","bad\nHeader"}}.keys()){
            bool rejected=false;try{const QJsonObject invalid{{bad,bad=="referer"?"https://player.test/\r\nCookie: forbidden":bad=="userAgent"?"bad\nHeader":"forbidden"}};downshift::browser::validate({{"version",1},{"command","media"},{"url",base+"/hls-default.txt"},{"title","Default"},{"context",invalid}});}catch(...){rejected=true;}check(rejected,"Unsafe media context rejected");}
        window.openVideo(base+"/hls-default.txt","Default stream",folder,context);auto* dialog=window.findChild<QDialog*>("mediaDialog");auto* submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());
        auto* quality=dialog->findChild<QComboBox*>("mediaQuality");check(quality->count()==1 && quality->currentData().toInt()==0 && !quality->isEnabled(),"Unknown resolution can download at default quality");
        submit->click();wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete" || submit->isEnabled();});check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());
        const auto entry=queue.snapshot().get().entries.back();check(entry.videoMetadata.value("requestContext").toObject()==context,"Retry context retained in encrypted history");dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
        for(const auto kind:QStringList{"direct","hls"}){
            const auto folder=QDir(root).filePath("pause-"+kind),database=QDir(root).filePath("pause-"+kind+".sqlite");QDir().mkpath(folder);
            auto queue=std::make_unique<downshift::QueueManager>(database);auto window=std::make_unique<downshift::MainWindow>(*queue);window->show();wait([&]{return window->ready();});auto settings=window->current().settings;settings.directory=folder;settings.notifications=false;settings.automatic=false;settings.connections=1;queue->configure(settings).get();
            const auto address=base+(kind=="direct"?"/pause.mp4":"/pause-stream.m3u8");window->openVideo(address,"Pause test",folder);auto* media=dynamic_cast<downshift::MediaDialog*>(window->findChild<QDialog*>("mediaDialog"));auto* submit=media->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});submit->click();
            wait([&]{const auto rows=queue->snapshot().get().entries;return rows.size()==1&&rows[0].progress.downloadedBytes>32768&&media->canPause();});const auto id=queue->snapshot().get().entries[0].id;auto* table=window->findChild<QTableWidget*>("downloadTable");wait([&]{return table->rowCount()==1;});table->selectRow(0);wait([&]{return window->findChild<QPushButton*>("pauseButton")->isEnabled();});
            window->findChild<QPushButton*>("pauseButton")->click();wait([&]{return queue->snapshot().get().entries[0].status==downshift::QueueStatus::Paused&&media->paused();});check(media->paused(),"Media controller enters paused state");
            const auto paused=queue->snapshot().get().entries[0];check(paused.id==id&&paused.progress.downloadedBytes>0&&paused.progress.smoothedBytesPerSecond==0,"Pause retains received count and clears speed on same task");
            const auto stage=QDir(folder).filePath(".downshift-video-"+id);const auto files=[&]{QMap<QString,qint64> result;for(const auto& file:QDir(stage).entryInfoList(QDir::Files|QDir::Hidden))result[file.fileName()]=file.size();return result;};const auto held=files();check(!held.isEmpty(),"Video partial files retained on pause");
            for(int tick=0;tick<80;++tick){QApplication::processEvents();QThread::msleep(5);}check(files()==held,"Paused helper stops writing partial files");wait([&]{return window->current().entries[0].status==downshift::QueueStatus::Paused;});check(window->findChild<QPushButton*>("startButton")->text()=="Resume"&&window->findChild<QPushButton*>("startButton")->isEnabled(),"Main toolbar offers Resume instead of Retry video");
            media->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);check(!window->findChild<QDialog*>("mediaDialog"),"Paused view closes without cancelling retained task");window.reset();queue.reset();
            queue=std::make_unique<downshift::QueueManager>(database);window=std::make_unique<downshift::MainWindow>(*queue);window->show();wait([&]{return window->ready();});check(queue->snapshot().get().entries[0].status==downshift::QueueStatus::Paused&&files()==held,"Paused media and its partial files survive queue/app-owner restart");table=window->findChild<QTableWidget*>("downloadTable");wait([&]{return table->rowCount()==1;});table->selectRow(0);window->findChild<QPushButton*>("startButton")->click();
            wait([&]{const auto rows=queue->snapshot().get().entries;return rows[0].status==downshift::QueueStatus::Completed||rows[0].status==downshift::QueueStatus::Failed;});const auto result=queue->snapshot().get().entries[0];check(result.status==downshift::QueueStatus::Completed,result.error.toUtf8().constData());check(result.id==id&&queue->snapshot().get().entries.size()==1,"Resume completes the same history entry without duplicating it");check(!QFileInfo::exists(stage),"Verified resumed completion removes only owned staging");
        }
        {
        const auto folder=QDir(root).filePath("browser-youtube");QDir().mkpath(folder);downshift::QueueManager queue(QDir(root).filePath("browser-youtube.sqlite"));downshift::MainWindow window(queue);window.show();
        const QJsonArray formats{QJsonObject{{"url",base+"/browser-video.mp4"},{"format_id","137"},{"ext","mp4"},{"vcodec","unknown"},{"acodec","none"},{"height",240},{"filesize",static_cast<double>(QFileInfo(QDir(root).filePath("../source/browser-video.mp4")).size())}},QJsonObject{{"url",base+"/browser-audio.m4a"},{"format_id","140"},{"ext","m4a"},{"vcodec","none"},{"acodec","unknown"},{"filesize",static_cast<double>(QFileInfo(QDir(root).filePath("../source/browser-audio.m4a")).size())}}};
        const QJsonObject context{{"youtube",QJsonObject{{"id","FQmqRVK3kHg"},{"duration",2.0},{"formats",formats}}}};
        downshift::browser::validate({{"version",1},{"command","media"},{"url","https://www.youtube.com/watch?v=FQmqRVK3kHg"},{"title","Browser source"},{"context",context}});++checks;
        bool rejected=false;try{downshift::browser::validate({{"version",1},{"command","media"},{"url","https://www.youtube.com/watch?v=HyfC9nZvIJM"},{"title","Other video"},{"context",context}});}catch(...){rejected=true;}check(rejected,"Browser descriptor cannot be attached to a different YouTube video");
        auto unsafe=context;auto video=unsafe["youtube"].toObject();auto injected=formats;auto item=injected[0].toObject();item["url"]="file:///C:/Windows/win.ini";injected[0]=item;video["formats"]=injected;unsafe["youtube"]=video;
        rejected=false;try{downshift::browser::validate({{"version",1},{"command","media"},{"url","https://www.youtube.com/watch?v=FQmqRVK3kHg"},{"title","Unsafe"},{"context",unsafe}});}catch(...){rejected=true;}check(rejected,"Browser descriptors reject local-file protocols");
        window.openVideo("https://www.youtube.com/watch?v=FQmqRVK3kHg",QString::fromUtf8("Browser source 日本語"),folder,context);auto* dialog=window.findChild<QDialog*>("mediaDialog");auto* submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());
        check(dialog->findChild<QComboBox*>("mediaQuality")->currentText().contains("240p"),"Browser's actual resolution is offered without a YouTube webpage check");submit->click();wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete"||submit->isEnabled();});check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());
        const auto saved=queue.snapshot().get().entries[0];check(saved.status==downshift::QueueStatus::Completed,"Browser audio/video transfer becomes durably completed only after verification");check(saved.destination.contains(QString::fromUtf8("日本語")),"Browser descriptor preserves Unicode names through Windows helper stdin/stdout");check(saved.videoMetadata.value("requestContext")==context,"Browser media descriptors persist inside encrypted video history");check(saved.videoMetadata.value("browserSource")==QJsonValue(true),"Actual browser fallback source mode persists for restart/resume");dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        const auto hlsFolder=QDir(root).filePath("browser-youtube-hls");QDir().mkpath(hlsFolder);const QJsonObject hlsContext{{"youtube",QJsonObject{{"id","HyfC9nZvIJM"},{"duration",2.0},{"formats",QJsonArray{}},{"manifest",base+"/hls-default.txt"}}},{"referer",base+"/embed/player"},{"origin","https://player.fixture.test"},{"userAgent","DownshiftMediaFixture/1.0"}};
        window.openVideo("https://www.youtube.com/watch?v=HyfC9nZvIJM","Browser HLS",hlsFolder,hlsContext);dialog=window.findChild<QDialog*>("mediaDialog");submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());submit->click();wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete"||submit->isEnabled();});check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        for(const auto height:{120,240}){
            const auto selectedFolder=QDir(root).filePath("browser-quality-"+QString::number(height));QDir().mkpath(selectedFolder);
            auto master=hlsContext;auto descriptor=master["youtube"].toObject();descriptor["manifest"]=base+"/hls-master.txt";master["youtube"]=descriptor;
            window.openVideo("https://www.youtube.com/watch?v=HyfC9nZvIJM","Browser master quality",selectedFolder,master);dialog=window.findChild<QDialog*>("mediaDialog");submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});
            auto* qualities=dialog->findChild<QComboBox*>("mediaQuality");check(qualities->findData(120)>=0&&qualities->findData(240)>=0&&qualities->isEnabled(),"Browser master exposes both real selectable resolutions");qualities->setCurrentIndex(qualities->findData(height));submit->click();wait([&]{return dialog->findChild<QLabel*>("mediaRate")->text()=="Complete"||submit->isEnabled();});check(dialog->findChild<QLabel*>("mediaRate")->text()=="Complete",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        // Reproduce the user's format-18 failure: it passes metadata, then the
        // source refuses the helper's transfer preflight. This used to crash
        // with KeyError: extractor because the supplied info was incomplete.
        const auto deniedFolder=QDir(root).filePath("browser-denied");QDir().mkpath(deniedFolder);
        const QJsonArray muxed{QJsonObject{{"url",base+"/browser-denied.mp4"},{"format_id","18"},{"ext","mp4"},{"vcodec","unknown"},{"acodec","unknown"},{"height",240},{"filesize",static_cast<double>(QFileInfo(QDir(root).filePath("../source/fixture.mp4")).size())}}};
        const QJsonObject deniedContext{{"youtube",QJsonObject{{"id","FQmqRVK3kHg"},{"duration",2.0},{"formats",muxed}}}};
        window.openVideo("https://www.youtube.com/watch?v=FQmqRVK3kHg","Denied browser source",deniedFolder,deniedContext);dialog=window.findChild<QDialog*>("mediaDialog");submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});check(submit->text()=="Download video",dialog->findChild<QLabel*>("mediaStatus")->text().toUtf8().constData());
        check(dialog->findChild<QComboBox*>("mediaQuality")->currentText()=="240p","Known single resolution is labeled explicitly, without Default quality");
        {QFile reject(QDir(root).filePath("../source/.reject-browser"));check(reject.open(QIODevice::WriteOnly),"Private expired-source marker opens");reject.write("reject");}
        submit->click();wait([&]{return submit->isEnabled();});const auto failure=dialog->findChild<QLabel*>("mediaStatus")->text();check(failure.contains("YouTube rejected")&&!failure.contains("KeyError")&&!failure.contains("Traceback"),failure.toUtf8().constData());
        check(dialog->findChild<QComboBox*>("mediaQuality")->count()==0,"Rejected browser formats are cleared rather than offered as downloadable");
        check(queue.snapshot().get().entries.back().status==downshift::QueueStatus::Failed,"Rejected source never becomes a completed download");dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        {const auto folder=QDir(root).filePath("lifecycle");QDir().mkpath(folder);const auto database=QDir(root).filePath("lifecycle.sqlite");downshift::QueueManager queue(database);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready();});
        auto settings=window.current().settings;settings.directory=folder;settings.notifications=false;queue.configure(settings).get();
        window.openVideo(base+"/background.mp4","Background",folder);QPointer<downshift::MediaDialog> dialog=dynamic_cast<downshift::MediaDialog*>(window.findChild<QDialog*>("mediaDialog"));check(dialog!=nullptr,"Background video dialog exists");
        auto* submit=dialog->findChild<QPushButton*>("mediaDownload");wait([&]{return submit->isEnabled();});
        check(dialog->windowFlags().testFlag(Qt::WindowMinimizeButtonHint),"Video window offers minimize");auto* name=dialog->findChild<QLineEdit*>("mediaTitle");name->setText("../unsafe");name->setModified(true);submit->click();check(queue.snapshot().get().entries.empty(),"Unsafe video filename never starts a transfer");
        name->setText("Renamed 100% video.mp4");name->setModified(true);
        {QFile existing(QDir(folder).filePath("Renamed 100% video.mp4"));check(existing.open(QIODevice::WriteOnly),"Collision fixture opens");existing.write("preserve existing file");}
        {QFile gate(QDir(root).filePath("../source/.hold-background"));check(gate.open(QIODevice::WriteOnly),"Hold private media response");}
        submit->click();wait([&]{return !queue.snapshot().get().entries.empty()&&dialog->findChild<QPushButton*>("mediaCancel")->isEnabled();});
        dialog->showMinimized();check(dialog->isMinimized(),"Active video window minimizes");dialog->present();dialog->close();check(dialog&&!dialog->isVisible(),"Closing active video hides its window");
        check(queue.snapshot().get().entries[0].status==downshift::QueueStatus::Running,"Closing video does not cancel history");
        window.openVideo(base+"/background.mp4","Same task",folder);check(dialog&&window.findChildren<QDialog*>("mediaDialog").size()==1&&dialog->isVisible(),"Hidden active video reopens without a duplicate task");dialog->reject();check(dialog&&!dialog->isVisible(),"Escape/reject hides an active video without deleting it");
        if(QSystemTrayIcon::isSystemTrayAvailable()){
            wait([&]{return window.current().entries.size()==1;});window.close();check(!window.isVisible()&&dialog,"Main close keeps active media alive in tray mode");check(!QApplication::quitOnLastWindowClosed(),"Closing all windows keeps the application event loop alive");
            check(window.findChild<QMenu*>("trayMenu")->actions().last()->objectName()=="trayExit","Tray provides explicit Exit action");
        }
        {QFile release(QDir(root).filePath("../source/.release-background"));check(release.open(QIODevice::WriteOnly),"Release private media response");}
        wait([&]{const auto rows=queue.snapshot().get().entries;return rows.size()==1&&rows[0].status==downshift::QueueStatus::Completed;});wait([&]{return !dialog;});
        const auto completed=queue.snapshot().get().entries[0];check(QFileInfo(completed.destination).fileName()=="Renamed 100% video (1).mp4","Edited video name, literal percent and collision suffix preserved");check(completed.videoMetadata.value("downloadName")=="Renamed 100% video.mp4","Edited video name retained for retries");
        window.showDownloads();check(window.isVisible(),"Tray Show restores main window");
        downshift::DownloadEngine::Inspection inspection;const auto receipt=queue.prepare(QString(32,'d'),base+"/fixture.mp4","original.exe",inspection).get();queue.handoff(QString(32,'d'),"commit",true).get();
        wait([&]{return window.findChild<QDialog*>("captureDialog-"+receipt.value("downloadId").toString())!=nullptr;});auto* capture=window.findChild<QDialog*>("captureDialog-"+receipt.value("downloadId").toString());auto* captureName=capture->findChild<QLineEdit*>("captureFilename");captureName->setText("NUL");capture->findChild<QPushButton*>("captureStart")->click();check(queue.snapshot().get().entries.back().status==downshift::QueueStatus::Paused,"Reserved browser filename rejected before starting");captureName->setText("Edited picture.png");check(QFileInfo(capture->findChild<QLineEdit*>("captureDestination")->text()).fileName()=="Edited picture.png","Browser name field updates destination");capture->findChild<QPushButton*>("captureStart")->click();wait([&]{return queue.snapshot().get().entries.back().status==downshift::QueueStatus::Completed;});check(QFileInfo(queue.snapshot().get().entries.back().destination).fileName()=="Edited picture.png","Browser handoff uses edited filename");
        wait([&]{return window.current().entries.size()==2;});auto* table=window.findChild<QTableWidget*>("downloadTable");window.findChild<QPushButton*>("closeDetails")->click();auto* panel=window.findChild<QWidget*>("detailsPanel");check(!panel->isVisible(),"Details initially closed for context-menu regression");const auto point=table->visualItemRect(table->item(0,0)).center();
        QMouseEvent right(QEvent::MouseButtonPress,QPointF(point),QPointF(table->viewport()->mapToGlobal(point)),Qt::RightButton,Qt::RightButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&right);bool menuSeen=false;
        QTimer::singleShot(20,&window,[&]{if(auto* menu=window.findChild<QMenu*>("downloadContextMenu")){menuSeen=true;menu->close();}});table->customContextMenuRequested(point);check(menuSeen&&!panel->isVisible(),"Right click shows actions without opening details");
        for(int n=0;n<60;++n){QApplication::processEvents();QThread::msleep(5);}check(!panel->isVisible(),"Background refresh cannot reopen right-clicked details");
        QMouseEvent left(QEvent::MouseButtonPress,QPointF(point),QPointF(table->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&left);QMouseEvent up(QEvent::MouseButtonRelease,QPointF(point),QPointF(table->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&up);check(panel->isVisible(),"Left click opens details even on the same context-selected row");
        window.openSettings();auto* config=window.findChild<QDialog*>("settingsDialog");auto* tray=config->findChild<QCheckBox*>("settingsCloseToTray");check(tray&&tray->isChecked(),"General settings offers enabled close-to-tray option");tray->setChecked(false);config->findChild<QPushButton*>("settingsSave")->click();wait([&]{return !window.current().settings.closeToTray;});check(!downshift::QueueSettings::fromJson(queue.snapshot().get().settings.json()).closeToTray,"Tray option round-trips through encrypted settings");window.requestExit();wait([&]{return !window.isVisible();});
        }
        std::cout<<"Media GUI checks passed: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
