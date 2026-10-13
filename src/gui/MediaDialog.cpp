#include "MediaDialog.h"
#include "core/DownloadQueue/VideoUrl.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace downshift {
namespace {
QString redacted(QString text) {
    // Helper stderr may contain signed links. Keep errors in memory and redact addresses.
    const auto words=text.split(' ');QStringList safe;
    for(const auto& word:words)safe.append(word.contains("http://") || word.contains("https://") ? "[address]":word);
    return safe.join(' ').right(1200);
}
QString byteSize(double value) {
    if(!std::isfinite(value) || value<0)return "—";
    return value>=1048576 ? QString::number(value/1048576.0,'f',2)+" MiB":QString::number(value/1024.0,'f',1)+" KiB";
}
}
MediaDialog::MediaDialog(QueueManager& queue,QString url,QString title,QWidget* parent,QString initialFolder):QDialog(parent),queue_(queue),url_(std::move(url)),tools_(QDir(QCoreApplication::applicationDirPath()).filePath("media-tools")),folder_(std::move(initialFolder)) {
    setAttribute(Qt::WA_DeleteOnClose);setWindowTitle("Download video — Downshift");resize(580,370);
    setProperty("mediaUrl",url_);setObjectName("mediaDialog");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(24,22,24,22);layout->setSpacing(14);title_=new QLabel(title.isEmpty() ? "Video":title);title_->setObjectName("mediaTitle");title_->setWordWrap(true);title_->setTextFormat(Qt::PlainText);layout->addWidget(title_);
    auto* source=new QLabel(QUrl(url_).host());source->setTextFormat(Qt::PlainText);layout->addWidget(source);
    auto* form=new QFormLayout;quality_=new QComboBox;quality_->setObjectName("mediaQuality");quality_->setEnabled(false);form->addRow("Quality",quality_);
    destination_=new QLineEdit;destination_->setObjectName("mediaDestination");destination_->setReadOnly(true);
    auto* folder=new QHBoxLayout;folder->addWidget(destination_);browse_=new QPushButton("Choose folder");browse_->setEnabled(false);folder->addWidget(browse_);form->addRow("Save to",folder);layout->addLayout(form);
    status_=new QLabel("Checking video…");status_->setObjectName("mediaStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    progress_=new QProgressBar;progress_->setObjectName("mediaProgress");progress_->setRange(0,0);layout->addWidget(progress_);
    rate_=new QLabel;rate_->setObjectName("mediaRate");layout->addWidget(rate_);
    auto* note=new QLabel("Videos appear in All downloads and Completed. Keep Downshift open while downloading. Protected and login-only videos may be unavailable.");note->setWordWrap(true);layout->addWidget(note);
    auto* actions=new QHBoxLayout;download_=new QPushButton("Download video");download_->setObjectName("mediaDownload");download_->setProperty("primary",true);download_->setEnabled(false);cancel_=new QPushButton("Cancel");actions->addWidget(download_);actions->addWidget(cancel_);layout->addLayout(actions);
    for(auto* button:{download_,cancel_,browse_}){button->setMinimumHeight(40);button->setCursor(Qt::PointingHandCursor);}
    cancel_->setObjectName("mediaCancel");openFolder_=new QPushButton("Open folder");openFolder_->setObjectName("mediaOpenFolder");openFolder_->hide();actions->insertWidget(1,openFolder_);
    connect(openFolder_,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(folder_));});
    connect(download_,&QPushButton::clicked,this,[this]{download();});connect(cancel_,&QPushButton::clicked,this,[this]{if(persisting_)return;if(process_.state()==QProcess::NotRunning)close();else cancel();});
    connect(browse_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Save video to",folder_);if(!path.isEmpty()){folder_=path;destination_->setText(folder_);}});
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{readOutput();});
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{errors_.append(process_.readAllStandardError());if(errors_.size()>8192)errors_=errors_.right(8192);});
    connect(&process_,&QProcess::started,this,[this]{
        job_=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const auto child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,static_cast<DWORD>(process_.processId()));
        const bool attached=job_ && SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) && child && AssignProcessToJobObject(job_,child);
        if(child)CloseHandle(child);
        if(!attached){if(job_)CloseHandle(job_);job_=nullptr;lastError_="Could not establish cancellation for the video helper.";process_.kill();}
    });
    connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){finished(code,status);});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){deadline_.stop();settle(QueueStatus::Failed,"Video helper could not start. Run scripts/Install-MediaTools.ps1, then retry.");}});
    deadline_.setSingleShot(true);connect(&deadline_,&QTimer::timeout,this,[this]{lastError_=verifying_ ? "The saved video could not be verified in time.":"The site did not finish checking this video. Try again.";if(job_)TerminateJobObject(job_,1);else process_.kill();});
    // The queue owns database access. Wait through a short event-loop timer, never .get() on GUI.
    auto pending=std::make_shared<std::future<QueueSnapshot>>(queue.snapshot());auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{settings_=pending->get().settings;if(!folder_.isEmpty()){destination_->setText(folder_);inspect();return;}auto folderResult=std::make_shared<std::future<QString>>(queue_.categoryFolder("video.mp4"));auto* folderTimer=new QTimer(this);folderTimer->setInterval(30);connect(folderTimer,&QTimer::timeout,this,[this,folderTimer,folderResult]{if(folderResult->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;folderTimer->stop();folderTimer->deleteLater();try{folder_=folderResult->get();destination_->setText(folder_);inspect();}catch(const std::exception& e){retryable(QString::fromUtf8(e.what()));}});folderTimer->start();}catch(...){status_->setText("Could not read download settings.");progress_->setRange(0,100);}});timer->start();
}
MediaDialog::~MediaDialog(){process_.disconnect(this);deadline_.stop();if(job_){CloseHandle(job_);job_=nullptr;}if(process_.state()!=QProcess::NotRunning)process_.kill();if(!historyId_.isEmpty())queue_.finishMedia(historyId_,QueueStatus::Cancelled,{},"Video window closed before completion.");}
QStringList MediaDialog::baseArguments() const {
    return {"--ignore-config","--no-plugin-dirs","--no-playlist","--no-colors","--socket-timeout","20","--retries","3","--fragment-retries","3","--js-runtimes","deno:"+QDir(tools_).filePath("deno.exe"),"--ffmpeg-location",tools_};
}
void MediaDialog::inspect(){verifying_=false;ready_=false;quality_->clear();status_->setText("Checking available qualities…");auto args=baseArguments();args << "--dump-single-json" << "--skip-download" << "--" << url_;launch(args,true);}
void MediaDialog::launch(QStringList arguments,bool metadata) {
    output_.clear();errors_.clear();lastError_.clear();metadata_=metadata;cancelled_=false;download_->setEnabled(false);browse_->setEnabled(false);quality_->setEnabled(false);cancel_->setText("Cancel");progress_->setRange(0,0);
    process_.setProgram(QDir(tools_).filePath("yt-dlp.exe"));process_.setArguments(arguments);process_.start();if(metadata)deadline_.start(90000);
}
void MediaDialog::readOutput() {
    output_.append(process_.readAllStandardOutput());
    if(output_.size()>4*1024*1024){lastError_="The video's metadata exceeded the supported limit.";cancelled_=true;if(job_)TerminateJobObject(job_,1);else process_.kill();return;}
    if(metadata_ || verifying_)return;
    while(output_.contains('\n') || (process_.state()==QProcess::NotRunning && !output_.isEmpty())) {
        const auto index=output_.contains('\n') ? output_.indexOf('\n'):output_.size();const auto line=output_.left(index).trimmed();output_.remove(0,index+1);
        if(line.startsWith("DS_PROGRESS ")) {
            const auto value=QJsonDocument::fromJson(line.mid(12)).object();const auto received=value.value("downloaded_bytes").toDouble();auto total=value.value("total_bytes").toDouble();if(total<=0)total=value.value("total_bytes_estimate").toDouble();
            if(total>0){progress_->setRange(0,1000);progress_->setValue(static_cast<int>(std::clamp(received/total,0.0,0.99)*1000));}
            status_->setText(value.value("status")=="finished" ? "Stream downloaded. Finishing video…":"Downloading video…");
            rate_->setText(byteSize(received)+" downloaded · "+byteSize(value.value("speed").toDouble())+"/s · "+(value.value("eta").isDouble() ? QString::number(value.value("eta").toInt())+" s remaining":"calculating time"));
            const auto now=std::chrono::steady_clock::now();
            if(!historyId_.isEmpty() && now-lastProgress_>=std::chrono::milliseconds(250)) {
                DownloadProgress p;p.downloadedBytes=static_cast<std::uint64_t>(std::clamp(received,0.0,1e15));if(total>0)p.totalBytes=static_cast<std::uint64_t>(std::min(total,1e15));
                p.smoothedBytesPerSecond=std::clamp(value.value("speed").toDouble(),0.0,1e15);p.elapsedSeconds=std::chrono::duration<double>(now-started_).count();p.averageBytesPerSecond=p.elapsedSeconds>0 ? std::clamp(received/p.elapsedSeconds,0.0,1e15):0;
                if(value.value("eta").isDouble())p.etaSeconds=std::max(0.0,value.value("eta").toDouble());queue_.mediaProgress(historyId_,p);lastProgress_=now;
            }
        }else if(line.startsWith("DS_FILE ")) {
            const auto document=QJsonDocument::fromJson("["+line.mid(8)+"]");
            if(document.isArray() && document.array().size()==1 && document.array()[0].isString())savedFile_=document.array()[0].toString();
        }
    }
}
void MediaDialog::finished(int code,QProcess::ExitStatus exitStatus) {
    deadline_.stop();readOutput();errors_.append(process_.readAllStandardError());if(job_){CloseHandle(job_);job_=nullptr;}
    progress_->setRange(0,100);cancel_->setText("Close");
    if(cancelled_ || !lastError_.isEmpty() || code!=0 || exitStatus!=QProcess::NormalExit) {
        settle(cancelled_ ? QueueStatus::Cancelled:QueueStatus::Failed,!lastError_.isEmpty() ? lastError_:cancelled_ ? "Stopped. Partial files are kept; retry to continue where the site permits.":"This video could not be downloaded or verified. "+redacted(QString::fromUtf8(errors_)));return;
    }else if(verifying_) {
        const auto info=QJsonDocument::fromJson(output_).object();bool video=false;
        for(const auto& stream:info.value("streams").toArray())if(stream.toObject().value("codec_type")=="video")video=true;
        if(!video || info.value("format").toObject().value("duration").toString().toDouble()<=0) {settle(QueueStatus::Failed,"The saved output is not a valid recorded video. Retry the download.");return;}
        settle(QueueStatus::Completed);return;
    }else if(metadata_) {
        const auto document=QJsonDocument::fromJson(output_);const auto info=document.object();QSet<int> heights;bool playable=false;
        for(const auto& value:info.value("formats").toArray()) {const auto format=value.toObject();const auto codec=format.value("vcodec").toString();if(format.value("has_drm").toBool() || codec=="none" || codec=="images" || format.value("protocol")=="mhtml" || format.value("url").toString().isEmpty())continue;playable=true;const auto height=format.value("height").toInt();if(height>0)heights.insert(height);}
        if(info.isEmpty() || info.value("_type")=="playlist" || info.value("_type")=="multi_video" || !playable || info.value("has_drm").toBool() || info.value("is_live").toBool()) {
            retryable(info.value("_type")=="playlist" || info.contains("entries") ? "This address is a feed or playlist, not one video. Open the video and use its player button, or paste its watch link.":"No downloadable video formats were found. Try a public recorded video link.");
        }
        else {
            title_->setText(info.value("title").toString(title_->text()).left(300));quality_->addItem("Best available",0);auto sorted=heights.values();std::sort(sorted.begin(),sorted.end(),std::greater<int>());for(int height:sorted)quality_->addItem(QString::number(height)+"p",height);
            quality_->setEnabled(true);browse_->setEnabled(true);ready_=true;download_->setText("Download video");download_->setEnabled(true);status_->setText("Choose quality and press Download video. Separate streams are merged automatically.");
        }
    }else {verify();return;}
    if(closeAfter_)QTimer::singleShot(0,this,[this]{close();});
}
void MediaDialog::retryable(QString error) {
    verifying_=false;persisting_=false;rate_->clear();progress_->setRange(0,100);progress_->setValue(0);status_->setText(error);
    download_->setText(ready_ ? "Retry download":"Retry check");download_->setEnabled(true);cancel_->setEnabled(true);cancel_->setText("Close");
    if(ready_){quality_->setEnabled(true);browse_->setEnabled(true);}if(closeAfter_)QTimer::singleShot(0,this,[this]{close();});
}
void MediaDialog::settle(QueueStatus result,QString error) {
    deadline_.stop();
    if(historyId_.isEmpty()){retryable(error);return;}
    persisting_=true;download_->setEnabled(false);cancel_->setEnabled(false);status_->setText(result==QueueStatus::Completed ? "Saving verified video history…":error);
    auto pending=std::make_shared<std::future<void>>(queue_.finishMedia(historyId_,result,savedFile_,error));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending,result,error]{
        if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();
        try {pending->get();historyId_.clear();persisting_=false;verifying_=false;
            if(result==QueueStatus::Completed){progress_->setRange(0,100);progress_->setValue(100);rate_->setText("Complete");status_->setText("Saved: "+savedFile_);destination_->setText(savedFile_);openFolder_->show();cancel_->setEnabled(true);cancel_->setText("Close");if(closeAfter_)close();}
            else retryable(error);
        }catch(const std::exception& e){
            if(result==QueueStatus::Completed){settle(QueueStatus::Failed,QString::fromUtf8(e.what()));}
            else {historyId_.clear();retryable("Could not save video history. "+QString::fromUtf8(e.what()));}
        }
    });timer->start();
}
void MediaDialog::verify() {
    if(savedFile_.isEmpty() || savedFile_.contains(QChar(0)) || savedFile_.size()>240 || !QDir::isAbsolutePath(savedFile_)) {settle(QueueStatus::Failed,"The downloader returned no final video filename. No completed file was verified.");return;}
    // ffprobe opens only local file protocols, asynchronously, after yt-dlp's final move.
    verifying_=true;output_.clear();errors_.clear();lastError_.clear();status_->setText("Download merged. Verifying the saved video…");rate_->setText("Verifying saved file");progress_->setRange(0,0);cancel_->setText("Cancel");
    process_.setProgram(QDir(tools_).filePath("ffprobe.exe"));process_.setArguments({"-v","error","-protocol_whitelist","file","-show_streams","-show_format","-of","json","-i",savedFile_});process_.start();deadline_.start(30000);
}
void MediaDialog::download() {
    if(!ready_){inspect();return;}
    if(folder_.isEmpty()){status_->setText("Choose a download folder first.");browse_->setEnabled(true);return;}
    auto args=baseArguments();const auto height=quality_->currentData().toInt();const QString cap=height>0 ? "[height="+QString::number(height)+"]":"";
    // All helpers are mandatory and verified by setup. Missing helpers report errors
    // asynchronously; avoid filesystem probes on the GUI thread.
    const auto regular="bv*"+cap+"+ba/b"+cap;
    // Public YouTube can advertise direct HTTPS formats that GVS rejects with
    // 403. Prefer its available HLS formats, then checked direct alternatives.
    const auto format=youtubeHost(QUrl(url_).host()) ? "bv*"+cap+"[protocol^=m3u8]+ba[protocol^=m3u8]/b"+cap+"[protocol^=m3u8]/"+regular:regular;
    if(youtubeHost(QUrl(url_).host()))args << "--check-formats";
    args << "--format" << format << "--abort-on-unavailable-fragments" << "--merge-output-format" << "mkv" << "--no-overwrites" << "--continue" << "--windows-filenames" << "--trim-filenames" << "150" << "--paths" << folder_ << "--output" << "%(title).120B [%(id).40B]%(height& [{}p]|)s.%(ext)s" << "--newline" << "--progress" << "--progress-template" << "download:DS_PROGRESS %(progress)j" << "--print" << "after_move:DS_FILE %(filepath)j" << "--no-simulate" << "--concurrent-fragments" << QString::number(settings_.automatic ? 4:std::min(settings_.connections,8));
    if(settings_.speedLimit)args << "--limit-rate" << QString::number(settings_.speedLimit);
    args << "--" << url_;status_->setText("Saving video to download history…");persisting_=true;download_->setEnabled(false);cancel_->setEnabled(false);browse_->setEnabled(false);quality_->setEnabled(false);savedFile_.clear();openFolder_->hide();destination_->setText(folder_);
    auto pending=std::make_shared<std::future<QString>>(queue_.beginMedia(url_,folder_,title_->text()));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending,args]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();persisting_=false;cancel_->setEnabled(true);
        try{historyId_=pending->get();started_=std::chrono::steady_clock::now();lastProgress_=started_-std::chrono::seconds(1);status_->setText("Starting video download…");verifying_=false;launch(args,false);}catch(const std::exception& e){retryable(QString::fromUtf8(e.what()));}
    });timer->start();
}
void MediaDialog::cancel(){cancelled_=true;status_->setText("Stopping…");if(job_)TerminateJobObject(job_,1);else process_.kill();}
void MediaDialog::closeEvent(QCloseEvent* event) {
    if(persisting_){event->ignore();return;}
    if(process_.state()!=QProcess::NotRunning){event->ignore();if(QMessageBox::question(this,"Stop video download?","Stop this video task and close its window? Partial files will be kept.")==QMessageBox::Yes){closeAfter_=true;cancel();}return;}
    QDialog::closeEvent(event);
}
}
