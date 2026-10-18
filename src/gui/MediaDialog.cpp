#include "MediaDialog.h"
#include "Controls.h"
#include "DownloadName.h"
#include "core/DownloadQueue/VideoUrl.h"
#include "core/DownloadQueue/MediaResume.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
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
MediaDialog::MediaDialog(QueueManager& queue,QString url,QString title,QWidget* parent,QString initialFolder,QJsonObject context,std::optional<QueueEntry> retained):QDialog(parent),queue_(queue),url_(std::move(url)),tools_(QDir(QCoreApplication::applicationDirPath()).filePath("media-tools")),folder_(std::move(initialFolder)),context_(std::move(context)),retained_(std::move(retained)) {
    setAttribute(Qt::WA_DeleteOnClose);setWindowFlags(Qt::Window|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint);setWindowTitle("Download video — Downshift");resize(580,370);
    setProperty("mediaUrl",url_);if(retained_)setProperty("mediaId",retained_->id);setObjectName("mediaDialog");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(24,22,24,22);layout->setSpacing(14);auto* nameLabel=new QLabel("Name");layout->addWidget(nameLabel);title_=new QLineEdit(suggestedDownloadName(title));title_->setObjectName("mediaTitle");title_->setAccessibleName("Video filename");title_->setToolTip("Edit before starting. The video format supplies the extension.");nameLabel->setBuddy(title_);layout->addWidget(title_);
    auto* source=new QLabel(QUrl(url_).host());source->setTextFormat(Qt::PlainText);layout->addWidget(source);
    auto* form=new QFormLayout;quality_=new QComboBox;modernCombo(quality_);quality_->setObjectName("mediaQuality");quality_->setEnabled(false);form->addRow("Quality",quality_);
    destination_=new QLineEdit;destination_->setObjectName("mediaDestination");destination_->setReadOnly(true);
    auto* folder=new QHBoxLayout;folder->addWidget(destination_);browse_=new QPushButton("Choose folder");browse_->setEnabled(false);folder->addWidget(browse_);form->addRow("Save to",folder);layout->addLayout(form);
    status_=new QLabel("Checking video…");status_->setObjectName("mediaStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    progress_=new QProgressBar;progress_->setObjectName("mediaProgress");progress_->setRange(0,0);layout->addWidget(progress_);
    rate_=new QLabel;rate_->setObjectName("mediaRate");layout->addWidget(rate_);
    auto* note=new QLabel("Closing this window keeps an active download running. Cancel stops it. Protected and login-only videos may be unavailable.");note->setWordWrap(true);layout->addWidget(note);
    auto* actions=new QHBoxLayout;download_=new QPushButton("Download video");download_->setObjectName("mediaDownload");download_->setProperty("primary",true);download_->setEnabled(false);cancel_=new QPushButton("Cancel");actions->addWidget(download_);pause_=new QPushButton("Pause");pause_->setObjectName("mediaPause");pause_->setEnabled(false);actions->addWidget(pause_);actions->addWidget(cancel_);connect(pause_,&QPushButton::clicked,this,[this]{if(paused_)resume();else pause();});layout->addLayout(actions);
    for(auto* button:{download_,cancel_,browse_,pause_}){button->setMinimumHeight(40);button->setCursor(Qt::PointingHandCursor);}
    cancel_->setObjectName("mediaCancel");openFolder_=new QPushButton("Open folder");openFolder_->setObjectName("mediaOpenFolder");openFolder_->hide();actions->insertWidget(1,openFolder_);
    connect(openFolder_,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(folder_));});
    connect(download_,&QPushButton::clicked,this,[this]{download();});connect(cancel_,&QPushButton::clicked,this,[this]{if(persisting_)return;if(paused_||process_.state()!=QProcess::NotRunning)cancel();else close();});
    connect(browse_,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Save video to",folder_);if(!path.isEmpty()){folder_=path;destination_->setText(folder_);}});
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{readOutput();});
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{errors_.append(process_.readAllStandardError());if(errors_.size()>8192)errors_=errors_.right(8192);});
    connect(&process_,&QProcess::started,this,[this]{
        job_=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const auto child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,static_cast<DWORD>(process_.processId()));
        const bool attached=job_ && SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) && child && AssignProcessToJobObject(job_,child);
        if(child)CloseHandle(child);
        pause_->setEnabled(canPause());if(!attached){if(job_)CloseHandle(job_);job_=nullptr;lastError_="Could not establish cancellation for the video helper.";process_.kill();}
        if(attached && !helperInput_.isEmpty()){process_.write(helperInput_);process_.closeWriteChannel();helperInput_.clear();}
    });
    connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){finished(code,status);});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){deadline_.stop();settle(QueueStatus::Failed,"Video helper could not start. Run scripts/Install-MediaTools.ps1, then retry.");}});
    deadline_.setSingleShot(true);connect(&deadline_,&QTimer::timeout,this,[this]{lastError_=verifying_ ? "The saved video could not be verified in time.":"The site did not finish checking this video. Try again.";if(job_)TerminateJobObject(job_,1);else process_.kill();});
    // The queue owns database access. Wait through a short event-loop timer, never .get() on GUI.
    auto pending=std::make_shared<std::future<QueueSnapshot>>(queue.snapshot());auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{settings_=pending->get().settings;if(retained_){restorePaused(*retained_);retained_.reset();if(property("resumeOnLoad").toBool())resume();return;}if(!folder_.isEmpty()){destination_->setText(folder_);inspect();return;}auto folderResult=std::make_shared<std::future<QString>>(queue_.categoryFolder("video.mp4"));auto* folderTimer=new QTimer(this);folderTimer->setInterval(30);connect(folderTimer,&QTimer::timeout,this,[this,folderTimer,folderResult]{if(folderResult->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;folderTimer->stop();folderTimer->deleteLater();try{folder_=folderResult->get();destination_->setText(folder_);inspect();}catch(const std::exception& e){retryable(QString::fromUtf8(e.what()));}});folderTimer->start();}catch(...){status_->setText("Could not read download settings.");progress_->setRange(0,100);}});timer->start();
}
MediaDialog::~MediaDialog(){process_.disconnect(this);deadline_.stop();if(job_){CloseHandle(job_);job_=nullptr;}if(process_.state()!=QProcess::NotRunning)process_.kill();if(!paused_&&!historyId_.isEmpty())queue_.finishMedia(historyId_,QueueStatus::Cancelled,{},"Video window closed before completion.");}
void MediaDialog::setDownloadName(const QString& name){if(!name.isEmpty()){title_->setText(name);title_->setModified(true);}}
void MediaDialog::refreshBrowserSource(const QJsonObject& context){
    if(!context.contains("youtube") || context==context_ || process_.state()!=QProcess::NotRunning || persisting_)return;
    context_=context;metadataInfo_["requestContext"]=context_;
    if(!paused_ && historyId_.isEmpty())inspect();
}
QStringList MediaDialog::baseArguments() const {
    QStringList args={"--ignore-config","--no-plugin-dirs","--no-playlist","--no-colors","--encoding","utf-8","--socket-timeout","20","--retries","3","--fragment-retries","3","--js-runtimes","deno:"+QDir(tools_).filePath("deno.exe"),"--ffmpeg-location",tools_};
    if(browserSource_ || !youtubeHost(QUrl(url_).host())){
        if(!context_.value("referer").toString().isEmpty())args << "--referer" << context_.value("referer").toString();
        if(!context_.value("origin").toString().isEmpty())args << "--add-headers" << "Origin:"+context_.value("origin").toString();
        if(!context_.value("userAgent").toString().isEmpty())args << "--user-agent" << context_.value("userAgent").toString();
    }
    if((context_.value("detectedStream").toBool() && !youtubeHost(QUrl(url_).host())) || sourceUrl()!=url_)args << "--force-generic-extractor";
    return args;
}
QString MediaDialog::sourceUrl() const {return browserSource_ ? context_.value("youtube").toObject().value("manifest").toString(url_):url_;}
bool MediaDialog::browserFormats() const {return browserSource_ && sourceUrl()==url_ && !context_.value("youtube").toObject().value("formats").toArray().isEmpty();}
QByteArray MediaDialog::browserInfo() const {
    const auto video=context_.value("youtube").toObject();auto formats=video.value("formats").toArray();
    for(auto it=formats.begin();it!=formats.end();++it){auto format=it->toObject();format["protocol"]=QUrl(format.value("url").toString()).scheme();format["downloader_options"]=QJsonObject{{"http_chunk_size",10485760}};*it=format;}
    // Build the minimal info ourselves; never accept helper programs, output
    // paths or postprocessors from browser data. Pipe it rather than saving
    // signed stream URLs to a plaintext temporary info file.
    // Error paths in the helper also require extractor, not only extractor_key.
    // Keep webpage_url absent: a rejected supplied stream must not silently
    // re-extract the anonymous YouTube watch page from --load-info-json.
    const auto json=QString::fromUtf8(QJsonDocument(QJsonObject{{"_type","video"},{"id",video.value("id")},{"title",title_->text()},{"duration",video.value("duration")},{"extractor","DownshiftBrowser"},{"extractor_key","Generic"},{"formats",formats}}).toJson(QJsonDocument::Compact));
    QByteArray ascii;for(const auto c:json){if(c.unicode()<128)ascii.append(static_cast<char>(c.unicode()));else ascii.append(QString("\\u%1").arg(static_cast<unsigned>(c.unicode()),4,16,QChar('0')).toLatin1());}return ascii;
}
void MediaDialog::inspect(){inspectSource(false);}
void MediaDialog::inspectSource(bool browserSource){
    if(process_.state()!=QProcess::NotRunning || persisting_)return;
    browserSource_=browserSource;
    verifying_=false;ready_=false;quality_->clear();status_->setText("Checking available qualities…");auto args=baseArguments();
    // Full info JSON includes every segment URL, subtitles and thumbnails. A
    // normal long video can exceed megabytes even with few quality choices.
    // Request only the fields used by this form, keeping the bounded parser.
    args << "--output-na-placeholder" << "null" << "--print" << "{\"meta\":%(.{id,title,_type,has_drm,is_live,thumbnail,webpage_url,duration,extractor_key,url,ext,protocol,vcodec,acodec,height})j,\"formats\":%(formats.:.{height,vcodec,protocol,has_drm,url})j}" << "--skip-download";
    if(youtubeHost(QUrl(url_).host()))args << "--socket-timeout" << "10" << "--retries" << "0" << "--extractor-retries" << "0";
    if(browserFormats())args << "--check-formats" << "--load-info-json" << "-";else args << "--" << sourceUrl();launch(args,true);
}
void MediaDialog::launch(QStringList arguments,bool metadata) {
    output_.clear();errors_.clear();lastError_.clear();metadata_=metadata;cancelled_=false;transferFinished_=false;pause_->setEnabled(false);download_->setEnabled(false);browse_->setEnabled(false);quality_->setEnabled(false);cancel_->setText("Cancel");progress_->setRange(0,0);
    helperInput_=browserFormats()?browserInfo():QByteArray();
    process_.setProgram(QDir(tools_).filePath("yt-dlp.exe"));process_.setArguments(arguments);process_.start();if(metadata)deadline_.start(90000);
}
void MediaDialog::readOutput() {
    output_.append(process_.readAllStandardOutput());
    if(output_.size()>4*1024*1024){lastError_=metadata_ ? "The site's quality summary is unexpectedly large. No download was started.":"The video helper returned too much output. Incomplete data will be removed.";output_.clear();if(job_)TerminateJobObject(job_,1);else process_.kill();return;}
    if(metadata_ || verifying_)return;
    while(output_.contains('\n') || (process_.state()==QProcess::NotRunning && !output_.isEmpty())) {
        const auto index=output_.contains('\n') ? output_.indexOf('\n'):output_.size();const auto line=output_.left(index).trimmed();output_.remove(0,index+1);
        if(line.startsWith("DS_PROGRESS ")) {
            const auto value=QJsonDocument::fromJson(line.mid(12)).object();const auto received=value.value("downloaded_bytes").toDouble();auto total=value.value("total_bytes").toDouble();if(total<=0)total=value.value("total_bytes_estimate").toDouble();
            if(total>0){progress_->setRange(0,1000);progress_->setValue(static_cast<int>(std::clamp(received/total,0.0,0.99)*1000));}
            transferFinished_=value.value("status")=="finished";pause_->setEnabled(canPause());status_->setText(value.value("status")=="finished" ? "Stream downloaded. Finishing video…":"Downloading video…");
            rate_->setText(byteSize(received)+" downloaded · "+byteSize(value.value("speed").toDouble())+"/s · "+(value.value("eta").isDouble() ? QString::number(value.value("eta").toInt())+" s remaining":"calculating time"));
            const auto now=std::chrono::steady_clock::now();
            if(!historyId_.isEmpty() && now-lastProgress_>=std::chrono::milliseconds(250)) {
                DownloadProgress p;p.downloadedBytes=static_cast<std::uint64_t>(std::clamp(received,0.0,1e15));if(total>0)p.totalBytes=static_cast<std::uint64_t>(std::min(total,1e15));
                p.smoothedBytesPerSecond=std::clamp(value.value("speed").toDouble(),0.0,1e15);p.elapsedSeconds=std::chrono::duration<double>(now-started_).count();elapsed_=p.elapsedSeconds;p.averageBytesPerSecond=p.elapsedSeconds>0 ? std::clamp(received/p.elapsedSeconds,0.0,1e15):0;
                if(value.value("eta").isDouble())p.etaSeconds=std::max(0.0,value.value("eta").toDouble());queue_.mediaProgress(historyId_,p);lastProgress_=now;
            }
        }else if(line.startsWith("DS_VIDEO ")) {const auto metadata=QJsonDocument::fromJson(line.mid(9)).object();for(auto it=metadata.begin();it!=metadata.end();++it)metadataInfo_[it.key()]=it.value();
        }else if(line.startsWith("DS_FILE ")) {
            const auto document=QJsonDocument::fromJson("["+line.mid(8)+"]");
            if(document.isArray() && document.array().size()==1 && document.array()[0].isString())savedFile_=document.array()[0].toString();
        }
    }
}
void MediaDialog::finished(int code,QProcess::ExitStatus exitStatus) {
    deadline_.stop();readOutput();errors_.append(process_.readAllStandardError());if(job_){CloseHandle(job_);job_=nullptr;}
    pause_->setEnabled(false);if(pauseRequested_){pauseRequested_=false;pausedResult();return;}
    progress_->setRange(0,100);cancel_->setText("Close");
    if(cancelled_ || !lastError_.isEmpty() || code!=0 || exitStatus!=QProcess::NormalExit) {
        // The site's extractor handles current signatures, complete quality
        // lists and client headers. A page's partial playback URL must never
        // override it. Browser descriptors are only a source-refusal fallback.
        if(metadata_ && !cancelled_ && lastError_.isEmpty() && !browserSource_ && context_.contains("youtube") && (errors_.contains("HTTP Error 429")||errors_.contains("HTTP Error 403")||errors_.contains("Sign in to confirm")||errors_.contains("Unable to download webpage")||errors_.contains("Unable to download API page"))){inspectSource(true);return;}
        if(!cancelled_ && lastError_.isEmpty() && context_.contains("youtube") && (errors_.contains("Requested format is not available") || errors_.contains("No video formats found"))){
            quality_->clear();ready_=false;settle(QueueStatus::Failed,"YouTube rejected the supplied media addresses. Refresh the playing video and use its Downshift button to obtain fresh streams.");return;
        }
        if(!cancelled_ && lastError_.isEmpty() && youtubeHost(QUrl(url_).host()) && errors_.contains("HTTP Error 429")){
            settle(QueueStatus::Failed,context_.contains("youtube") ? "YouTube refused the browser stream. Refresh the playing video in Chrome and use its Downshift button to refresh the source.":"YouTube refused the separate video check (HTTP 429). Start playback in Chrome and use the updated Downshift video button to send its loaded stream.");return;
        }
        if(metadata_ && !cancelled_ && lastError_.isEmpty() && !context_.value("detectedStream").toBool() && errors_.contains("Unsupported URL")){
            retryable("The site extractor cannot read this player page. Start playback in Chrome and use its Downshift video button to capture the actual stream.");return;
        }
        settle(cancelled_ ? QueueStatus::Cancelled:QueueStatus::Failed,!lastError_.isEmpty() ? lastError_:cancelled_ ? "Stopped. Incomplete video files were removed; Retry starts a fresh download.":"This video could not be downloaded or verified. "+redacted(QString::fromUtf8(errors_)));return;
    }else if(verifying_) {
        const auto info=QJsonDocument::fromJson(output_).object();bool video=false,audio=false;
        for(const auto& stream:info.value("streams").toArray()){if(stream.toObject().value("codec_type")=="video")video=true;if(stream.toObject().value("codec_type")=="audio")audio=true;}
        const auto duration=info.value("format").toObject().value("duration").toString().toDouble(),expected=browserSource_?context_.value("youtube").toObject().value("duration").toDouble():youtubeHost(QUrl(url_).host())?metadataInfo_.value("duration").toDouble():0.0;
        if(!video || duration<=0 || (expected>0 && (!audio || std::abs(duration-expected)>std::max(.5,expected*.01)))) {settle(QueueStatus::Failed,"The saved output is not a complete recorded video with the expected streams and duration. Retry the download.");return;}
        settle(QueueStatus::Completed);return;
    }else if(metadata_) {
        QJsonParseError parseError{};const auto document=QJsonDocument::fromJson(output_,&parseError);const auto summary=document.object(),info=summary.value("meta").toObject();QSet<int> heights;bool playable=false;
        if(parseError.error!=QJsonParseError::NoError || !document.isObject() || info.isEmpty()){retryable("The site returned an invalid video summary. Retry the check.");return;}
        auto formats=summary.value("formats").toArray();
        // Some extractors expose one real stream with no formats/resolution list.
        // Accept that stream as default; missing quality metadata is not a failure.
        if(formats.isEmpty() && !info.value("url").toString().isEmpty())formats.append(info);
        for(const auto& value:formats) {const auto format=value.toObject();const auto codec=format.value("vcodec").toString();const QUrl source(format.value("url").toString());if(format.value("has_drm").toBool() || codec=="none" || codec=="images" || format.value("protocol")=="mhtml" || !source.isValid() || source.host().isEmpty() || !source.userInfo().isEmpty() || (source.scheme()!="https" && source.scheme()!="http"))continue;playable=true;const auto height=format.value("height").toInt();if(height>0)heights.insert(height);}
        if(info.isEmpty() || info.value("_type")=="playlist" || info.value("_type")=="multi_video" || !playable || info.value("has_drm").toBool() || info.value("is_live").toBool()) {
            retryable(info.value("_type")=="playlist" || info.contains("entries") ? "This address is a feed or playlist, not one video. Open the video and use its player button, or paste its watch link.":"No downloadable video formats were found. Try a public recorded video link.");
        }
        else {
            metadataInfo_=info;metadataInfo_["requestContext"]=context_;metadataInfo_["browserSource"]=browserSource_;if(!title_->isModified())title_->setText(suggestedDownloadName(info.value("title").toString(title_->text())));
            quality_->addItem(heights.size()>1 ? "Best available":heights.isEmpty() ? "Default quality":QString::number(*heights.begin())+"p",heights.size()==1?*heights.begin():0);
            if(heights.size()>1){auto sorted=heights.values();std::sort(sorted.begin(),sorted.end(),std::greater<int>());for(int height:sorted)quality_->addItem(QString::number(height)+"p",height);}
            quality_->setEnabled(quality_->count()>1);browse_->setEnabled(true);ready_=true;download_->setText("Download video");download_->setEnabled(true);status_->setText(quality_->count()>1 ? "Choose quality and press Download video. Separate streams are merged automatically.":heights.isEmpty()?"Ready to download at the player's default quality.":"Ready to download at "+quality_->currentText()+".");
        }
    }else {verify();return;}
    if(closeAfter_)QTimer::singleShot(0,this,[this]{close();});
}
void MediaDialog::retryable(QString error) {
    verifying_=false;persisting_=false;pause_->setEnabled(false);rate_->clear();progress_->setRange(0,100);progress_->setValue(0);status_->setText(error);
    download_->setText(ready_ ? "Retry download":"Retry check");download_->setEnabled(true);cancel_->setEnabled(true);cancel_->setText("Close");
    title_->setReadOnly(false);if(ready_){quality_->setEnabled(quality_->count()>1);browse_->setEnabled(true);}if(closeAfter_)QTimer::singleShot(0,this,[this]{close();});
}
void MediaDialog::settle(QueueStatus result,QString error) {
    deadline_.stop();
    if(historyId_.isEmpty()){retryable(error);return;}
    persisting_=true;pause_->setEnabled(false);download_->setEnabled(false);cancel_->setEnabled(false);status_->setText(result==QueueStatus::Completed ? "Saving verified video history…":error);
    auto pending=std::make_shared<std::future<QString>>(queue_.finishMedia(historyId_,result,savedFile_,error,metadataInfo_));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending,result,error]{
        if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();
        try {const auto final=pending->get();if(result==QueueStatus::Completed)savedFile_=final;historyId_.clear();persisting_=false;verifying_=false;
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
    helperInput_.clear();process_.setProgram(QDir(tools_).filePath("ffprobe.exe"));process_.setArguments({"-v","error","-protocol_whitelist","file","-show_streams","-show_format","-of","json","-i",savedFile_});process_.start();deadline_.start(30000);
}
void MediaDialog::restorePaused(const QueueEntry& entry){
    if(!entry.media||entry.status!=QueueStatus::Paused||!validMediaResume(entry.videoMetadata.value("resumeRecipe").toObject())){retryable("This older video has no continuation checkpoint. Use Retry video.");return;}
    historyId_=entry.id;metadataInfo_=entry.videoMetadata;paused_=true;metadata_=false;ready_=true;elapsed_=entry.progress.elapsedSeconds;
    browserSource_=metadataInfo_.value("browserSource").toBool(context_.contains("youtube"));
    title_->setText(entry.mediaTitle);title_->setReadOnly(true);destination_->setText(folder_);quality_->clear();const auto height=metadataInfo_.value("resumeRecipe").toObject().value("height").toInt();quality_->addItem(height?QString::number(height)+"p":"Default quality",height);quality_->setEnabled(false);
    progress_->setRange(0,1000);progress_->setValue(entry.progress.totalBytes&&*entry.progress.totalBytes?static_cast<int>(std::min(990.,1000.*static_cast<double>(entry.progress.downloadedBytes)/static_cast<double>(*entry.progress.totalBytes))):0);
    rate_->setText(byteSize(static_cast<double>(entry.progress.downloadedBytes))+" retained · Paused");status_->setText("Paused. Resume continues this task's partial files.");download_->setEnabled(false);pause_->setText("Resume");pause_->setEnabled(true);cancel_->setText("Cancel");cancel_->setEnabled(true);
}
void MediaDialog::pause(){
    if(!canPause())return;pauseRequested_=true;elapsed_=std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count();pause_->setEnabled(false);cancel_->setEnabled(false);status_->setText("Pausing video and retaining partial files…");
    if(job_)TerminateJobObject(job_,1);else process_.kill();
}
void MediaDialog::pausedResult(){
    persisting_=true;auto pending=std::make_shared<std::future<void>>(queue_.pauseMedia(historyId_,true));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();persisting_=false;
        try{pending->get();paused_=true;pause_->setText("Resume");pause_->setEnabled(true);cancel_->setEnabled(true);cancel_->setText("Cancel");status_->setText("Paused. Partial files are retained for Resume.");rate_->setText("Paused");if(closeAfter_)close();}
        catch(const std::exception& e){settle(QueueStatus::Failed,QString::fromUtf8(e.what()));}
    });timer->start();
}
void MediaDialog::resume(){
    if(retained_){setProperty("resumeOnLoad",true);return;}if(!paused_||persisting_)return;
    persisting_=true;pause_->setEnabled(false);cancel_->setEnabled(false);status_->setText("Resuming retained video files…");auto pending=std::make_shared<std::future<void>>(queue_.pauseMedia(historyId_,false,context_));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();persisting_=false;cancel_->setEnabled(true);
        try{pending->get();paused_=false;pause_->setText("Pause");auto args=transferArguments();args[args.indexOf("--paths")+1]=QDir(folder_).filePath(".downshift-video-"+historyId_);started_=std::chrono::steady_clock::now()-std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(elapsed_));lastProgress_=std::chrono::steady_clock::now()-std::chrono::seconds(1);savedFile_.clear();verifying_=false;launch(args,false);}
        catch(const std::exception& e){pause_->setEnabled(true);status_->setText("Could not resume: "+QString::fromUtf8(e.what()));}
    });timer->start();
}
QStringList MediaDialog::transferArguments() const {
    auto args=baseArguments();const auto height=metadataInfo_.value("resumeRecipe").toObject().value("height").toInt();const QString cap=height>0 ? "[height="+QString::number(height)+"]":"";
    // All helpers are mandatory and verified by setup. Missing helpers report errors
    // asynchronously; avoid filesystem probes on the GUI thread.
    const auto regular="bv*"+cap+"+ba/b"+cap;
    // Public YouTube can advertise direct HTTPS formats that GVS rejects with
    // 403. Prefer its available HLS formats, then checked direct alternatives.
    const auto format=youtubeHost(QUrl(url_).host()) && !browserSource_ ? "bv*"+cap+"[protocol^=m3u8]+ba[protocol^=m3u8]/b"+cap+"[protocol^=m3u8]/"+regular:regular;
    if(youtubeHost(QUrl(url_).host()))args << "--check-formats";
    auto name=metadataInfo_.value("resumeRecipe").toObject().value("filename").toString();
    const auto outputName=name.isEmpty()?QString("%(title).120B [%(id).40B]%(height& [{}p]|)s.%(ext)s"):name.replace("%","%%")+".%(ext)s";
    args << "--format" << format << "--abort-on-unavailable-fragments" << "--merge-output-format" << "mkv" << "--no-overwrites" << "--continue" << "--windows-filenames" << "--trim-filenames" << "150" << "--paths" << folder_ << "--output" << outputName << "--newline" << "--progress" << "--progress-template" << "download:DS_PROGRESS %(progress)j" << "--print" << "after_move:DS_VIDEO %(.{title,thumbnail,webpage_url,duration,height,ext})j" << "--print" << "after_move:DS_FILE %(filepath)j" << "--no-simulate" << "--concurrent-fragments" << QString::number(settings_.automatic ? 4:std::min(settings_.connections,8));
    if(settings_.speedLimit)args << "--limit-rate" << QString::number(settings_.speedLimit);
    if(browserFormats())args << "--load-info-json" << "-";else args << "--" << sourceUrl();return args;
}
void MediaDialog::download() {
    if(!ready_){inspect();return;}
    if(folder_.isEmpty()){status_->setText("Choose a download folder first.");browse_->setEnabled(true);return;}
    if(!validDownloadName(title_->text())){status_->setText("Enter a valid Windows filename without folder separators (up to 150 characters).");title_->setFocus();return;}
    const auto height=quality_->currentData().toInt();
    if(title_->isModified()){
        auto name=title_->text();const auto suffix=QFileInfo(name).suffix().toLower();if(QStringList{"mp4","mkv","webm","mov","avi","m4v"}.contains(suffix))name.chop(suffix.size()+1);
        if(!validDownloadName(name)){status_->setText("Enter a video name before the extension.");return;}
        metadataInfo_["downloadName"]=title_->text();
    }
    title_->setReadOnly(true);
    metadataInfo_["resumeRecipe"]=QJsonObject{{"version",1},{"height",height},{"filename",title_->isModified()?metadataInfo_.value("downloadName").toString():QString()}};
    if(title_->isModified()){auto name=title_->text();const auto suffix=QFileInfo(name).suffix().toLower();if(QStringList{"mp4","mkv","webm","mov","avi","m4v"}.contains(suffix))name.chop(suffix.size()+1);auto recipe=metadataInfo_["resumeRecipe"].toObject();recipe["filename"]=name;metadataInfo_["resumeRecipe"]=recipe;}
    auto args=transferArguments();status_->setText("Saving video to download history…");persisting_=true;download_->setEnabled(false);cancel_->setEnabled(false);browse_->setEnabled(false);quality_->setEnabled(false);savedFile_.clear();openFolder_->hide();destination_->setText(folder_);
    metadataInfo_["height"]=height;metadataInfo_["originalPage"]=url_;
    auto pending=std::make_shared<std::future<QString>>(queue_.beginMedia(url_,folder_,title_->text(),metadataInfo_));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending,args]() mutable {if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();persisting_=false;cancel_->setEnabled(true);
        try{historyId_=pending->get();setProperty("mediaId",historyId_);const auto index=args.indexOf("--paths");if(index>=0)args[index+1]=QDir(folder_).filePath(".downshift-video-"+historyId_);started_=std::chrono::steady_clock::now();lastProgress_=started_-std::chrono::seconds(1);status_->setText("Starting video download…");verifying_=false;launch(args,false);}catch(const std::exception& e){retryable(QString::fromUtf8(e.what()));}
    });timer->start();
}
void MediaDialog::cancel(){if(paused_){paused_=false;settle(QueueStatus::Cancelled,"Cancelled. Incomplete video files were removed.");return;}cancelled_=true;status_->setText("Stopping…");if(job_)TerminateJobObject(job_,1);else process_.kill();}
void MediaDialog::closeEvent(QCloseEvent* event) {
    if(active()){event->ignore();closeAfter_=true;hide();return;}
    QDialog::closeEvent(event);
}
}
