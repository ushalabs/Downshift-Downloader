#include "QueueManager.h"
#include "Categories.h"
#include "VideoUrl.h"
#include "database/QueueStore.h"
#include "core/FileWriter/FileWriter.h"
#include "core/ResumeManager/ResumeJournal.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
namespace downshift {
namespace {
std::filesystem::path path(const QString& s) { return std::filesystem::path(s.toStdWString()); }
[[noreturn]] void invalid(const char* message) { throw std::runtime_error(message); }
QJsonObject object(const QByteArray& bytes) {
    QJsonParseError e{}; const auto doc = QJsonDocument::fromJson(bytes, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value("version").toInt() != 1) invalid("Unsupported or damaged queue data.");
    return doc.object();
}
QString origin(const QString& text) {
    const QUrl url(text, QUrl::StrictMode);
    if (text.size() > 32768 || text.contains(QChar(0)) || !url.isValid() || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")) invalid("Enter a valid HTTP or HTTPS URL without embedded credentials.");
    return url.scheme() + "://" + url.host().toLower() + ":" + QString::number(url.port(url.scheme() == "https" ? 443 : 80));
}
QString destination(const QString& text) { return QString::fromStdWString(FileWriter::validatedDestination(path(text)).wstring()); }
}
QString statusText(QueueStatus s) {
    switch (s) { case QueueStatus::Queued: return "Queued"; case QueueStatus::Running: return "Downloading";
    case QueueStatus::Paused: return "Paused"; case QueueStatus::Completed: return "Completed"; case QueueStatus::Failed: return "Failed";
    case QueueStatus::Cancelled: return "Cancelled"; case QueueStatus::Prepared: return "Awaiting Chrome"; } return "Unknown";
}
QJsonObject QueueSettings::json() const { return {{"version",1},{"directory",directory},{"simultaneous",simultaneous},{"perHost",perHost},{"hostConnections",hostConnections},{"connections",connections},{"automatic",automatic},{"dark",dark},{"startOnLaunch",startOnLaunch},{"notifications",notifications},{"diagnostics",diagnostics},{"speedLimit",QString::number(speedLimit)}}; }
QueueSettings QueueSettings::fromJson(const QJsonObject& o) {
    QueueSettings s; s.directory = o.value("directory").toString(); s.simultaneous = o.value("simultaneous").toInt(3); s.perHost = o.value("perHost").toInt(2);
    s.hostConnections = o.value("hostConnections").toInt(16); s.connections = o.value("connections").toInt(16);
    s.automatic = o.value("automatic").toBool(true); s.dark = o.value("dark").toBool(); s.startOnLaunch = o.value("startOnLaunch").toBool();
    s.notifications = o.value("notifications").toBool(true); s.diagnostics = o.value("diagnostics").toBool(); s.speedLimit = o.value("speedLimit").toString().toULongLong();
    if (s.simultaneous < 1 || s.simultaneous > 16 || s.perHost < 1 || s.perHost > 16 || s.hostConnections < 1 || s.hostConnections > 64 || s.connections < 1 || s.connections > 16 || s.speedLimit > 10ULL*1024*1024*1024) invalid("Invalid queue settings.");
    return s;
}
QJsonObject QueueEntry::json() const { return {{"version",1},{"id",id},{"url",url},{"destination",destination},{"origin",origin},{"error",error},{"sha256",sha256},{"status",static_cast<int>(status)},{"priority",priority},{"created",QString::number(created)},{"updated",QString::number(updated)},{"existing",static_cast<int>(existing)},{"speedLimit",QString::number(speedLimit)},
    {"media",media},{"mediaTitle",mediaTitle},{"mediaFolder",mediaFolder},
    {"downloaded",QString::number(progress.downloadedBytes)},{"verified",QString::number(progress.verifiedBytes)},{"total",progress.totalBytes ? QString::number(*progress.totalBytes) : QString()},{"resumable",progress.resumable}, {"journal",destination+".part.resume"},
    {"averageSpeed",progress.averageBytesPerSecond},{"elapsed",progress.elapsedSeconds},{"targetConnections",static_cast<int>(progress.targetConnections)},
    {"handoffKey",handoffKey},{"requiredEtag",requiredEtag},{"requiredResolvedUrl",requiredResolvedUrl},{"requiredTotal",requiredTotal ? QString::number(*requiredTotal):QString()}}; }
QueueEntry QueueEntry::fromJson(const QJsonObject& o) {
    QueueEntry e; e.id=o.value("id").toString(); e.url=o.value("url").toString(); e.destination=o.value("destination").toString(); e.origin=downshift::origin(e.url);
    e.media=o.value("media").toBool();e.mediaTitle=o.value("mediaTitle").toString();e.mediaFolder=o.value("mediaFolder").toString();
    e.error=o.value("error").toString(); e.sha256=o.value("sha256").toString(); e.status=static_cast<QueueStatus>(o.value("status").toInt(-1)); e.priority=o.value("priority").toInt();
    e.created=o.value("created").toString().toLongLong(); e.updated=o.value("updated").toString().toLongLong(); e.existing=static_cast<ExistingDownload>(o.value("existing").toInt(-1)); e.speedLimit=o.value("speedLimit").toString().toULongLong();
    e.progress.downloadedBytes=o.value("downloaded").toString().toULongLong(); e.progress.verifiedBytes=o.value("verified").toString().toULongLong();
    const auto total=o.value("total").toString(); if(!total.isEmpty()) e.progress.totalBytes=total.toULongLong(); e.progress.resumable=o.value("resumable").toBool();
    e.progress.averageBytesPerSecond=o.value("averageSpeed").toDouble();e.progress.elapsedSeconds=o.value("elapsed").toDouble();e.progress.targetConnections=static_cast<std::uint32_t>(o.value("targetConnections").toInt(1));
    e.handoffKey=o.value("handoffKey").toString();e.requiredEtag=o.value("requiredEtag").toString();e.requiredResolvedUrl=o.value("requiredResolvedUrl").toString();if(!o.value("requiredTotal").toString().isEmpty())e.requiredTotal=o.value("requiredTotal").toString().toULongLong();
    if(e.progress.averageBytesPerSecond<0 || e.progress.averageBytesPerSecond>1e15 || e.progress.elapsedSeconds<0 || e.progress.elapsedSeconds>1e12 || e.progress.targetConnections<1 || e.progress.targetConnections>16) invalid("Invalid saved download metrics.");
    if(e.id.size()!=32 || e.status < QueueStatus::Queued || e.status > QueueStatus::Prepared || e.existing < ExistingDownload::New || e.existing > ExistingDownload::Restart || e.priority < -1 || e.priority > 1 || e.destination.isEmpty()) invalid("Damaged download record.");
    return e;
}
class QueueManager::RateBudget {
    std::mutex mutex; std::uint64_t rate=0, generation=0;
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
public:
    void configure(std::uint64_t value) { std::lock_guard lock(mutex); rate=value; ++generation; next=std::chrono::steady_clock::now(); }
    void wait(std::size_t count, std::stop_token stop, DownloadControl* control) {
        std::chrono::steady_clock::time_point due; std::uint64_t version;
        { std::lock_guard lock(mutex); if (!rate) return; version=generation; next=std::max(next,std::chrono::steady_clock::now()) + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(static_cast<double>(count)/static_cast<double>(rate))); due=next; }
        while (std::chrono::steady_clock::now() < due) {
            { std::lock_guard lock(mutex); if(generation!=version) return; }
            if (stop.stop_requested()) throw DownloadError(DownloadErrorCode::Cancelled,"Download cancelled; partial files preserved.");
            if (control->pauseRequested()) throw DownloadError(DownloadErrorCode::Paused,"Download paused.");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};
struct QueueManager::Worker {
    DownloadControl control;
    std::mutex mutex;
    DownloadProgress progress;
    bool done=false, cancel=false;
    QueueStatus status=QueueStatus::Running;
    QString error;
    std::jthread thread; // Declared last: joins before control and mailbox destruction.
};
QueueManager::QueueManager(QString database):database_(std::move(database)),budget_(std::make_shared<RateBudget>()),service_([this]{run();}) {}
QueueManager::~QueueManager() {
    { std::lock_guard lock(mutex_); closing_=true; } condition_.notify_one(); service_.join();
}
void QueueManager::run() {
    QLockFile lease(database_+".owner"); lease.setStaleLockTime(0);
    try {
        if (!QDir().mkpath(QFileInfo(database_).absolutePath()) || !lease.tryLock()) invalid("Another Downshift instance owns this queue.");
        store_=std::make_unique<QueueStore>(database_);
        const auto saved=store_->settings(); if(!saved.isEmpty()) settings_=QueueSettings::fromJson(object(saved));
        if(settings_.directory.isEmpty()) settings_.directory=QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        budget_->configure(settings_.speedLimit);
        for(const auto& row:store_->receipts()) {const auto r=object(row);const auto key=r.value("key").toString(),state=r.value("state").toString();if(key.size()!=32 || receipts_.contains(key) || (state!="prepared" && state!="committed" && state!="aborted"))invalid("Damaged browser handoff receipt.");receipts_.emplace(key,r);}
        for(const auto& row:store_->load()) {
            auto e=QueueEntry::fromJson(object(row));
            if(!e.handoffKey.isEmpty() && (!receipts_.contains(e.handoffKey) || receipts_.at(e.handoffKey).value("downloadId")!=e.id))invalid("Missing or mismatched browser handoff receipt.");
            if(!e.media && !youtubeVideo(e.url).isEmpty() && e.status!=QueueStatus::Failed && e.status!=QueueStatus::Cancelled) {e.status=QueueStatus::Failed;e.error="This earlier attempt used a YouTube webpage rather than video streams. Choose Retry video. Existing files are preserved.";save(e);}
            if(e.media && (e.status==QueueStatus::Running || e.status==QueueStatus::Queued)) {e.status=QueueStatus::Failed;e.error="Video task interrupted. Choose Retry video to continue in a video window.";save(e);}
            else if(e.status==QueueStatus::Running || (e.status==QueueStatus::Queued && !settings_.startOnLaunch)) { e.status=QueueStatus::Paused; e.error="Interrupted or held at startup. Choose Resume to continue."; save(e); }
            e.progress.activeRanges=0;e.progress.smoothedBytesPerSecond=e.progress.instantaneousBytesPerSecond=0;e.progress.etaSeconds.reset();
            if(std::any_of(entries_.begin(),entries_.end(),[&](const auto& old){return old.id==e.id;})) invalid("Duplicate database download IDs.");
            entries_.push_back(std::move(e));
        }
    } catch (...) { fatal_=std::current_exception(); }
    for(;;) {
        std::deque<std::function<void()>> tasks; bool closing;
        { std::unique_lock lock(mutex_); condition_.wait_for(lock,std::chrono::milliseconds(50),[this]{return closing_ || !tasks_.empty();}); tasks.swap(tasks_); closing=closing_; }
        for(auto& task:tasks) task();
        if(closing) break;
        if(!fatal_) try { tick(); schedule(); } catch(...) { fatal_=std::current_exception(); for(auto& [id,w]:workers_) w->control.requestPause(); }
    }
    for(auto& [id,w]:workers_) w->control.requestPause();
    for(auto& [id,w]:workers_) if(w->thread.joinable()) w->thread.join();
    if(!fatal_) try { tick(); } catch(...) {}
    workers_.clear(); store_.reset(); // Release database before releasing process lease.
}
QueueEntry& QueueManager::entry(const QString& id) { const auto i=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.id==id;}); if(i==entries_.end()) invalid("Download no longer exists."); return *i; }
void QueueManager::save(QueueEntry& e) {
    e.updated=QDateTime::currentSecsSinceEpoch();
    try { store_->save(e.id,QJsonDocument(e.json()).toJson(QJsonDocument::Compact)); }
    catch (...) { fatal_=std::current_exception(); throw; } // Fail closed before scheduling unpersisted work.
    if(settings_.diagnostics) {
        QFile log(database_+".log");
        if(log.open(log.size()>1024*1024 ? QIODevice::WriteOnly|QIODevice::Truncate : QIODevice::WriteOnly|QIODevice::Append))
            log.write(QString("%1 %2 %3\n").arg(e.updated).arg(e.id).arg(statusText(e.status)).toUtf8());
    }
}
std::future<QString> QueueManager::add(QString url,QString dest,int priority,bool start,QString sha256,std::uint64_t speedLimit) {
    return submit([this,url=std::move(url),dest=std::move(dest),priority,start,sha256=std::move(sha256),speedLimit] {
        if(youtubeHost(QUrl(url).host())) invalid("This is a YouTube page, not a file. Use Download video to resolve its streams and qualities.");
        if(entries_.size()>=10000 || priority < -1 || priority > 1 || speedLimit > 10ULL*1024*1024*1024) invalid("Queue limit or invalid download options.");
        QueueEntry e; e.url=url; e.origin=origin(url); e.destination=destination(dest); e.sha256=sha256.toLower();
        const auto parent=QFileInfo(e.destination).absolutePath();
        for(const auto& category:QStringList{"Videos","Audios","Zip","Programs"})if(parent.compare(QDir(settings_.directory).filePath(category),Qt::CaseInsensitive)==0 && !QDir().mkpath(parent))invalid("The category folder could not be created.");
        if(!e.sha256.isEmpty() && (e.sha256.size()!=64 || std::any_of(e.sha256.begin(),e.sha256.end(),[](QChar c){return !QString("0123456789abcdef").contains(c);}))) invalid("SHA-256 must contain 64 hexadecimal characters.");
        for(const auto& old:entries_) if(old.destination.compare(e.destination,Qt::CaseInsensitive)==0) invalid("A download already owns this destination. Choose a different filename.");
        if(QFileInfo::exists(e.destination) || QFileInfo::exists(e.destination+".part") || QFileInfo::exists(e.destination+".part.resume")) invalid("The destination or its partial already exists. Choose a different filename.");
        e.id=QString::fromStdString(ResumeJournal::newId()); e.priority=priority; e.speedLimit=speedLimit; e.created=QDateTime::currentSecsSinceEpoch(); e.status=start ? QueueStatus::Queued : QueueStatus::Paused;
        save(e); entries_.push_back(e); return e.id;
    });
}
std::future<void> QueueManager::action(QString id,QueueAction action) { return submit([this,id=std::move(id),action] {
    auto& e=entry(id); const auto worker=workers_.find(id);
    if(!e.media && !youtubeVideo(e.url).isEmpty() && (action==QueueAction::Start || action==QueueAction::Restart)) invalid("Use Retry video to download the streams, rather than the YouTube webpage.");
    if(e.media && (action!=QueueAction::Remove || e.status==QueueStatus::Running)) invalid("Use the video window to stop this task, or Retry video to retry it.");
    if(e.status==QueueStatus::Prepared) invalid("Chrome still owns this handoff. Leave it in Chrome or cancel the pending handoff.");
    if(!e.handoffKey.isEmpty() && receipts_.at(e.handoffKey).value("state")=="aborted" && (action==QueueAction::Start || action==QueueAction::Restart)) invalid("This browser handoff was aborted. Add a fresh download link instead.");
    if(action==QueueAction::HighPriority || action==QueueAction::NormalPriority || action==QueueAction::LowPriority) { e.priority=action==QueueAction::HighPriority ? 1 : action==QueueAction::LowPriority ? -1 : 0; save(e); return; }
    if(worker!=workers_.end()) {
        if(action==QueueAction::Pause) worker->second->control.requestPause();
        else if(action==QueueAction::Cancel) { worker->second->cancel=true; worker->second->thread.request_stop(); }
        else if(action!=QueueAction::Start) invalid("Pause the download before changing or removing it.");
        return;
    }
    if(action==QueueAction::Remove) { store_->erase(id); std::erase_if(entries_,[&](const auto& row){return row.id==id;}); return; }
    if(e.status==QueueStatus::Completed) invalid("This download is already complete.");
    if(action==QueueAction::Start && e.status==QueueStatus::Paused && e.error.startsWith("Download paused") && e.progress.resumable && e.progress.downloadedBytes && !e.progress.verifiedBytes && QFileInfo::exists(e.destination+".part.resume")) {
        ResumeJournal journal(path(e.destination));
        if(journal.load().completed.empty())invalid("This earlier pause has no reusable checkpoint. Choose Restart explicitly; the old partial is preserved.");
    }
    if(action==QueueAction::Pause) e.status=QueueStatus::Paused;
    else if(action==QueueAction::Cancel) e.status=QueueStatus::Cancelled;
    else { e.existing=action==QueueAction::Restart ? ExistingDownload::Restart : QFileInfo::exists(e.destination+".part.resume") ? ExistingDownload::Resume : ExistingDownload::New; e.status=QueueStatus::Queued; e.error.clear(); }
    save(e);
}); }
std::future<void> QueueManager::rename(QString id,QString dest) { return submit([this,id=std::move(id),dest=std::move(dest)] {
    auto& e=entry(id); if(e.media || workers_.contains(id) || e.status==QueueStatus::Completed || QFileInfo::exists(e.destination+".part")) invalid("Only an unstarted file download can be renamed.");
    const auto d=destination(dest); for(const auto& old:entries_) if(old.id!=id && old.destination.compare(d,Qt::CaseInsensitive)==0) invalid("Another download owns that destination.");
    if(QFileInfo::exists(d) || QFileInfo::exists(d+".part") || QFileInfo::exists(d+".part.resume")) invalid("The destination already exists.");
    e.destination=d; save(e);
}); }
std::future<void> QueueManager::deleteCompleted(QString id) { return submit([this,id=std::move(id)] {
    auto& e=entry(id); if(e.status!=QueueStatus::Completed || workers_.contains(id)) invalid("Only completed files can be deleted here.");
    const auto p=path(e.destination);
    const auto handle=CreateFileW(p.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE) invalid("The completed file could not be opened for deletion.");
    BY_HANDLE_FILE_INFORMATION info{}; FILE_DISPOSITION_INFO disposition{TRUE};
    const bool safe=GetFileInformationByHandle(handle,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
    const bool deleted=safe && SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition)); CloseHandle(handle);
    if(!deleted) invalid("The completed file could not be safely deleted.");
    store_->erase(id); std::erase_if(entries_,[&](const auto& row){return row.id==id;});
}); }
std::future<void> QueueManager::configure(QueueSettings s) { return submit([this,s=std::move(s)]() mutable {
    s=QueueSettings::fromJson(s.json()); if(s.directory.isEmpty() || !QFileInfo(s.directory).isDir()) invalid("Select an existing download directory.");
    store_->settings(QJsonDocument(s.json()).toJson(QJsonDocument::Compact)); settings_=s; budget_->configure(s.speedLimit);
}); }
std::future<QueueSnapshot> QueueManager::snapshot() { return submit([this] { tick(); return QueueSnapshot{entries_,settings_,{}}; }); }
std::future<QString> QueueManager::categoryFolder(QString filename,QString mime) {return submit([this,filename=std::move(filename),mime=std::move(mime)]{const auto folder=QDir(settings_.directory).filePath(downloadCategory(filename,mime));if(!QDir().mkpath(folder))invalid("The download category folder could not be created.");return folder;});}
std::future<QString> QueueManager::beginMedia(QString url,QString folder,QString title) {
    return submit([this,url=std::move(url),folder=std::move(folder),title=std::move(title)] {
        if(entries_.size()>=10000 || !QFileInfo(folder).isDir()) invalid("Select an existing video folder, or remove old history if it is full.");
        QueueEntry e;e.url=url;e.origin=origin(url);e.media=true;e.mediaTitle=title.left(300);
        e.mediaFolder=QFileInfo(folder).canonicalFilePath();e.destination=e.mediaFolder;
        for(const auto& old:entries_)if(old.media && old.status==QueueStatus::Running && old.url==url && old.mediaFolder.compare(e.mediaFolder,Qt::CaseInsensitive)==0)invalid("This video is already downloading into that folder.");
        e.id=QString::fromStdString(ResumeJournal::newId());e.status=QueueStatus::Running;e.created=QDateTime::currentSecsSinceEpoch();save(e);entries_.push_back(e);return e.id;
    });
}
std::future<void> QueueManager::mediaProgress(QString id,DownloadProgress progress) {
    return submit([this,id=std::move(id),progress] {auto& e=entry(id);if(e.media && e.status==QueueStatus::Running)e.progress=progress;});
}
std::future<void> QueueManager::finishMedia(QString id,QueueStatus status,QString file,QString error) {
    return submit([this,id=std::move(id),status,file=std::move(file),error=std::move(error)] {
        auto& e=entry(id);if(!e.media || e.status!=QueueStatus::Running)invalid("This video task is no longer active.");
        if(status!=QueueStatus::Completed && status!=QueueStatus::Failed && status!=QueueStatus::Cancelled)invalid("Invalid video result.");
        if(status==QueueStatus::Completed) {
            // Completion is durable only for a nonempty, ordinary file in the chosen folder.
            // The asynchronous ffprobe stage in MediaDialog verifies playable video first.
            const auto d=destination(file);const QFileInfo info(d);
            if(!info.isAbsolute() || info.absoluteDir().canonicalPath().compare(e.mediaFolder,Qt::CaseInsensitive)!=0 || info.suffix()=="part")invalid("No final video was saved in the chosen folder.");
            for(const auto& old:entries_)if(!old.media && old.destination.compare(d,Qt::CaseInsensitive)==0)invalid("Another download owns this video filename.");
            const auto handle=CreateFileW(path(d).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(handle==INVALID_HANDLE_VALUE)invalid("The final video file is missing or unavailable.");
            BY_HANDLE_FILE_INFORMATION attributes{};LARGE_INTEGER size{};
            const bool valid=GetFileInformationByHandle(handle,&attributes) && !(attributes.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && GetFileSizeEx(handle,&size) && size.QuadPart>0;
            CloseHandle(handle);if(!valid)invalid("The final video is empty or is not a regular file.");
            e.destination=d;e.progress.downloadedBytes=static_cast<std::uint64_t>(size.QuadPart);e.progress.totalBytes=e.progress.downloadedBytes;
        }
        e.status=status;e.error=error;e.progress.smoothedBytesPerSecond=0;e.progress.activeRanges=0;e.progress.etaSeconds.reset();save(e);
    });
}
void QueueManager::tick() {
    for(auto i=workers_.begin();i!=workers_.end();) {
        auto& w=*i->second; auto& e=entry(i->first); bool done;
        { std::lock_guard lock(w.mutex); e.progress=w.progress; done=w.done; if(done) { e.status=w.cancel ? QueueStatus::Cancelled : w.status; e.error=w.error; } }
        if(done) {e.progress.activeRanges=0;e.progress.smoothedBytesPerSecond=e.progress.instantaneousBytesPerSecond=0;e.progress.etaSeconds.reset();save(e); i=workers_.erase(i); } else ++i;
    }
}
void QueueManager::schedule() {
    std::vector<QueueEntry*> pending; for(auto& e:entries_) if(!e.media && e.status==QueueStatus::Queued) pending.push_back(&e);
    std::stable_sort(pending.begin(),pending.end(),[](auto* a,auto* b){return a->priority>b->priority;});
    for(auto* e:pending) {
        if(!youtubeVideo(e->url).isEmpty()){e->status=QueueStatus::Failed;e->error="This is a YouTube webpage. Choose Retry video to download its streams.";save(*e);continue;}
        if(workers_.size()>=static_cast<std::size_t>(settings_.simultaneous)) break;
        int host=0; for(const auto& [id,w]:workers_) if(entry(id).origin==e->origin) ++host;
        if(host>=std::min(settings_.perHost,settings_.hostConnections)) continue;
        auto w=std::make_unique<Worker>(); auto* mailbox=w.get();
        if(e->existing==ExistingDownload::Resume){w->progress=e->progress;w->progress.stage=DownloadStage::Probe;w->progress.activeRanges=0;w->progress.smoothedBytesPerSecond=w->progress.instantaneousBytesPerSecond=0;}
        DownloadOptions options; options.url=e->url.toStdString(); options.destination=path(e->destination); options.existing=e->existing; options.control=&mailbox->control;
        options.requiredEtag=e->requiredEtag.toStdString();options.requiredResolvedUrl=e->requiredResolvedUrl.toStdString();options.requiredTotal=e->requiredTotal;
        options.automaticConnections=settings_.automatic; options.maxConnections=static_cast<std::uint32_t>(std::min(settings_.connections,std::max(1,settings_.hostConnections/settings_.perHost)));
        if(!e->sha256.isEmpty()) options.expectedSha256=e->sha256.toStdString();
        auto local=std::make_shared<RateBudget>(); local->configure(e->speedLimit); auto global=budget_;
        options.beforeWrite=[local,global,mailbox](std::size_t n,std::stop_token stop) { local->wait(n,stop,&mailbox->control); global->wait(n,stop,&mailbox->control); };
        options.onProgress=[mailbox](const DownloadProgress& p) { std::lock_guard lock(mailbox->mutex); mailbox->progress=p; };
        e->status=QueueStatus::Running; save(*e);
        const auto id=e->id; workers_.emplace(id,std::move(w));
        try { mailbox->thread=std::jthread([mailbox,options=std::move(options)](std::stop_token stop) {
            QueueStatus status=QueueStatus::Completed; QString error;
            try { const auto result=DownloadEngine().download(options,stop); std::lock_guard lock(mailbox->mutex); mailbox->progress.downloadedBytes=result.bytes; mailbox->progress.verifiedBytes=result.bytes; mailbox->progress.totalBytes=result.bytes; }
            catch(const DownloadError& e) { status=e.code()==DownloadErrorCode::Paused ? QueueStatus::Paused : e.code()==DownloadErrorCode::Cancelled ? QueueStatus::Cancelled : QueueStatus::Failed; error=QString::fromUtf8(e.what()); }
            catch(...) { status=QueueStatus::Failed; error="The download worker failed. Your partial files have been preserved."; }
            { std::lock_guard lock(mailbox->mutex); mailbox->status=status; mailbox->error=error; mailbox->done=true; }
        }); } catch(...) { workers_.erase(id); e->status=QueueStatus::Failed; e->error="Unable to create a download worker."; save(*e); }
    }
}
std::future<QJsonObject> QueueManager::prepare(QString key,QString url,QString filename,DownloadEngine::Inspection inspection,QString mime) {
    return submit([this,key=std::move(key),url=std::move(url),filename=std::move(filename),inspection=std::move(inspection),mime=std::move(mime)] {
        if(youtubeHost(QUrl(url).host()))invalid("Use Download video for YouTube rather than saving its watch page.");
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(),QCryptographicHash::Sha256).toHex());
        if(const auto i=receipts_.find(key);i!=receipts_.end()) {if(i->second.value("urlHash")!=hash)invalid("This handoff key belongs to another request.");return QJsonObject{{"state",i->second.value("state")},{"downloadId",i->second.value("downloadId")}};}
        if(entries_.size()>=10000 || receipts_.size()>=10000)invalid("The browser handoff/history limit has been reached.");
        if(key.size()!=32 || std::any_of(key.begin(),key.end(),[](QChar c){return !QString("0123456789abcdef").contains(c);}))invalid("Invalid handoff key.");
        if(filename.isEmpty() || filename.size()>160 || filename.contains('/') || filename.contains('\\') || filename=="." || filename=="..")invalid("Invalid browser filename.");
        QueueEntry e;e.id=QString::fromStdString(ResumeJournal::newId());e.url=url;e.origin=origin(url);e.handoffKey=key;e.status=QueueStatus::Prepared;e.created=QDateTime::currentSecsSinceEpoch();e.updated=e.created;
        e.requiredEtag=QString::fromStdString(inspection.etag);if(!inspection.etag.empty()){e.requiredResolvedUrl=QString::fromStdString(inspection.resolvedUrl);e.requiredTotal=inspection.total;}if(inspection.total)e.progress.totalBytes=inspection.total;e.progress.resumable=!inspection.etag.empty();
        const auto folder=QDir(settings_.directory).filePath(downloadCategory(filename,mime));if(!QDir().mkpath(folder))invalid("The download category folder could not be created.");
        const QFileInfo name(filename); bool assigned=false;
        for(int index=0;index<1000;++index) {
            const auto candidate=index ? name.completeBaseName()+" ("+QString::number(index)+")"+(name.suffix().isEmpty() ? "":"."+name.suffix()):filename;
            const auto dest=destination(QDir(folder).filePath(candidate));
            if(QFileInfo::exists(dest) || QFileInfo::exists(dest+".part") || QFileInfo::exists(dest+".part.resume") || std::any_of(entries_.begin(),entries_.end(),[&](const auto& old){return old.destination.compare(dest,Qt::CaseInsensitive)==0;}))continue;
            e.destination=dest;assigned=true;break;
        }
        if(!assigned)invalid("No free download filename is available.");
        QJsonObject receipt{{"version",1},{"key",key},{"downloadId",e.id},{"urlHash",hash},{"state","prepared"}};
        try {store_->handoff(e.id,QJsonDocument(e.json()).toJson(QJsonDocument::Compact),key,QJsonDocument(receipt).toJson(QJsonDocument::Compact));}
        catch(...){fatal_=std::current_exception();throw;}
        receipts_.emplace(key,receipt);entries_.push_back(e);return QJsonObject{{"state","prepared"},{"downloadId",e.id}};
    });
}
std::future<QJsonObject> QueueManager::handoff(QString key,QString command,bool prompt) {return submit([this,key=std::move(key),command=std::move(command),prompt] {
    const auto i=receipts_.find(key);if(i==receipts_.end())return QJsonObject{{"state","unknown"}};
    auto receipt=i->second;const auto state=receipt.value("state").toString();const auto id=receipt.value("downloadId").toString();
    if(command!="status" && command!="commit" && command!="abort")invalid("Unsupported browser operation.");
    if(state=="prepared" && command!="status") {
        auto e=entry(id);e.status=command=="commit" ? (prompt ? QueueStatus::Paused:QueueStatus::Queued):QueueStatus::Cancelled;e.error=prompt && command=="commit" ? "Choose where to save this download.":QString();e.updated=QDateTime::currentSecsSinceEpoch();receipt["state"]=command=="commit" ? "committed":"aborted";
        try{store_->handoff(e.id,QJsonDocument(e.json()).toJson(QJsonDocument::Compact),key,QJsonDocument(receipt).toJson(QJsonDocument::Compact));}catch(...){fatal_=std::current_exception();throw;}
        entry(id)=e;i->second=receipt;
    }
    QJsonObject result{{"state",receipt.value("state")},{"downloadId",id}};
    const auto e=std::find_if(entries_.begin(),entries_.end(),[&](const auto& row){return row.id==id;});result["downloadStatus"]=e==entries_.end() ? "Removed":statusText(e->status);return result;
});}
}
