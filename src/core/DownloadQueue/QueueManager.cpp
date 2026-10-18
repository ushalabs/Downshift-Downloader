#include "QueueManager.h"
#include "MediaContext.h"
#include "MediaResume.h"
#include "Categories.h"
#include "VideoUrl.h"
#include "OwnedMedia.h"
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
QString browserOrigin(const QString& text) {
    if(text.isEmpty() || text.size()>32768 || text.contains(QChar(0)) || QUrl(text).scheme().isEmpty())invalid("Invalid browser source.");
    return "browser:"+QUrl(text).scheme();
}
void ordinaryParents(const std::filesystem::path& file) {
    for(auto p=file.parent_path();!p.empty() && p!=p.root_path();p=p.parent_path()) {
        const auto attributes=GetFileAttributesW(p.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))invalid("Choose a folder without directory links for this browser transfer.");
    }
}
}
QString statusText(QueueStatus s) {
    switch (s) { case QueueStatus::Queued: return "Queued"; case QueueStatus::Running: return "Downloading";
    case QueueStatus::Paused: return "Paused"; case QueueStatus::Completed: return "Completed"; case QueueStatus::Failed: return "Failed";
    case QueueStatus::Cancelled: return "Cancelled"; case QueueStatus::Prepared: return "Awaiting Chrome"; } return "Unknown";
}
QJsonObject QueueSettings::json() const { return {{"version",1},{"directory",directory},{"simultaneous",simultaneous},{"perHost",perHost},{"hostConnections",hostConnections},{"connections",connections},{"automatic",automatic},{"dark",dark},{"startOnLaunch",startOnLaunch},{"notifications",notifications},{"diagnostics",diagnostics},{"closeToTray",closeToTray},{"speedLimit",QString::number(speedLimit)}}; }
QueueSettings QueueSettings::fromJson(const QJsonObject& o) {
    QueueSettings s; s.directory = o.value("directory").toString(); s.simultaneous = o.value("simultaneous").toInt(3); s.perHost = o.value("perHost").toInt(2);
    s.hostConnections = o.value("hostConnections").toInt(16); s.connections = o.value("connections").toInt(16);
    s.automatic = o.value("automatic").toBool(true); s.dark = o.value("dark").toBool(); s.startOnLaunch = o.value("startOnLaunch").toBool();
    s.notifications = o.value("notifications").toBool(true); s.diagnostics = o.value("diagnostics").toBool(); s.speedLimit = o.value("speedLimit").toString().toULongLong();
    s.closeToTray=o.value("closeToTray").toBool(true);
    if (s.simultaneous < 1 || s.simultaneous > 16 || s.perHost < 1 || s.perHost > 16 || s.hostConnections < 1 || s.hostConnections > 64 || s.connections < 1 || s.connections > 16 || s.speedLimit > 10ULL*1024*1024*1024) invalid("Invalid queue settings.");
    return s;
}
QJsonObject QueueEntry::json() const { return {{"version",1},{"id",id},{"url",url},{"destination",destination},{"origin",origin},{"error",error},{"sha256",sha256},{"status",static_cast<int>(status)},{"priority",priority},{"created",QString::number(created)},{"updated",QString::number(updated)},{"existing",static_cast<int>(existing)},{"speedLimit",QString::number(speedLimit)},
    {"mediaIdentity",mediaIdentity},{"mediaStaged",mediaStaged},{"videoMetadata",videoMetadata},{"partialIdentity",partialIdentity},{"media",media},{"mediaTitle",mediaTitle},{"mediaFolder",mediaFolder},{"browserTransfer",browserTransfer},{"browserComplete",browserComplete},{"browserFile",browserFile},
    {"downloaded",QString::number(progress.downloadedBytes)},{"verified",QString::number(progress.verifiedBytes)},{"total",progress.totalBytes ? QString::number(*progress.totalBytes) : QString()},{"resumable",progress.resumable}, {"journal",destination+".part.resume"},
    {"averageSpeed",progress.averageBytesPerSecond},{"elapsed",progress.elapsedSeconds},{"targetConnections",static_cast<int>(progress.targetConnections)},
    {"handoffKey",handoffKey},{"requiredEtag",requiredEtag},{"requiredResolvedUrl",requiredResolvedUrl},{"requiredTotal",requiredTotal ? QString::number(*requiredTotal):QString()}}; }
QueueEntry QueueEntry::fromJson(const QJsonObject& o) {
    QueueEntry e; e.id=o.value("id").toString(); e.url=o.value("url").toString(); e.destination=o.value("destination").toString();e.browserTransfer=o.value("browserTransfer").toBool();e.browserComplete=o.value("browserComplete").toBool();e.browserFile=o.value("browserFile").toString();e.origin=e.browserTransfer ? browserOrigin(e.url):downshift::origin(e.url);
    if(e.browserTransfer && (o.value("media").toBool() || (!e.browserFile.isEmpty() && (!QFileInfo(e.browserFile).isAbsolute() || e.browserFile.size()>32768 || e.browserFile.contains(QChar(0))))))invalid("Damaged browser transfer record.");
    e.mediaIdentity=o.value("mediaIdentity").toString();e.mediaStaged=o.value("mediaStaged").toBool();e.videoMetadata=o.value("videoMetadata").toObject();e.partialIdentity=o.value("partialIdentity").toString();
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
            if (stop.stop_requested()) throw DownloadError(DownloadErrorCode::Cancelled,"Download cancelled.");
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
    QString error,partialIdentity;
    bool browser=false;
    std::atomic_bool browserPaused=false, browserComplete=false;
    std::atomic<qint64> browserSize=-1;
    std::atomic<qint64> browserReceived=0;
    std::atomic_bool browserFailed=false;
    std::jthread thread; // Declared last: joins before control and mailbox destruction.
};
QueueManager::QueueManager(QString database):database_(QFileInfo(database).absoluteFilePath()),budget_(std::make_shared<RateBudget>()),service_([this]{run();}) {}
QueueManager::~QueueManager() {
    { std::lock_guard lock(mutex_); closing_=true; } condition_.notify_one(); service_.join();
}
void QueueManager::initialize() {
        if(initialized_ || !workers_.empty())invalid("Close Downshift before reloading a queue that was already active.");
        if(!lease_){lease_=std::make_unique<QLockFile>(database_+".owner");lease_->setStaleLockTime(0);}
        if (!QDir().mkpath(QFileInfo(database_).absolutePath()) || (!lease_->isLocked() && !lease_->tryLock())) invalid("Another Downshift instance owns this queue. Close it, then choose Retry loading.");
        auto candidate=std::make_unique<QueueStore>(database_);QueueSettings settings;
        const auto saved=candidate->settings();if(!saved.isEmpty())settings=QueueSettings::fromJson(object(saved));if(settings.directory.isEmpty())settings.directory=QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        std::map<QString,QJsonObject> receipts;std::vector<QueueEntry> entries;
        for(const auto& row:candidate->receipts()) {const auto r=object(row);const auto key=r.value("key").toString(),state=r.value("state").toString();if(key.size()!=32 || receipts.contains(key) || (state!="prepared" && state!="committed" && state!="aborted"))invalid("Damaged browser handoff receipt.");receipts.emplace(key,r);}
        const auto rows=candidate->load();if(rows.empty())candidate->verifyEmptyHistory();
        for(const auto& row:rows) {
            auto e=QueueEntry::fromJson(object(row));
            if(!e.handoffKey.isEmpty() && (!receipts.contains(e.handoffKey) || receipts.at(e.handoffKey).value("downloadId")!=e.id))invalid("Missing or mismatched browser handoff receipt.");
            if(std::any_of(entries.begin(),entries.end(),[&](const auto& old){return old.id==e.id;}))invalid("Duplicate database download IDs.");entries.push_back(std::move(e));
        }
        // Validate every encrypted record before publishing state or updating recovery statuses.
        store_=std::move(candidate);settings_=std::move(settings);receipts_=std::move(receipts);entries_=std::move(entries);budget_->configure(settings_.speedLimit);
        try{store_->backup();backupWarning_.clear();}catch(const std::exception& error){backupWarning_=QString::fromUtf8(error.what());}
        for(auto& e:entries_){
            if(!e.media && !e.browserTransfer && !youtubeVideo(e.url).isEmpty() && e.status!=QueueStatus::Failed && e.status!=QueueStatus::Cancelled) {e.status=QueueStatus::Failed;e.error="This earlier attempt used a YouTube webpage rather than video streams. Choose Retry video. Existing files are preserved.";save(e);}
            if(e.media&&e.mediaStaged&&validMediaResume(e.videoMetadata.value("resumeRecipe").toObject())&&(e.status==QueueStatus::Running||e.status==QueueStatus::Queued)){e.status=QueueStatus::Paused;e.error="Video interrupted. Choose Resume to continue its retained partial files.";save(e);}
            else if(e.media && (e.status==QueueStatus::Running || e.status==QueueStatus::Queued)) {e.status=QueueStatus::Failed;e.error="Video task interrupted. Choose Retry video to start a fresh transfer.";if(e.mediaStaged)try{cleanup(e);}catch(const std::exception& error){e.error+=" Cleanup: "+QString::fromUtf8(error.what());}save(e);}
            else if(e.status==QueueStatus::Running || (e.status==QueueStatus::Queued && !settings_.startOnLaunch)) { e.status=QueueStatus::Paused; e.error="Interrupted or held at startup. Choose Resume to continue."; save(e); }
            if(e.mediaStaged&&(e.status==QueueStatus::Failed||e.status==QueueStatus::Cancelled)&&QFileInfo::exists(ownedMedia::workspace(e.mediaFolder,e.id)))try{cleanup(e);e.error+=" Incomplete video data removed after restart.";save(e);}catch(const std::exception& error){e.error+=" Cleanup: "+QString::fromUtf8(error.what());save(e);}
            e.progress.activeRanges=0;e.progress.smoothedBytesPerSecond=e.progress.instantaneousBytesPerSecond=0;e.progress.etaSeconds.reset();
        }
        initialized_=true;fatal_={};
}
std::future<void> QueueManager::retryLoading() {
    auto task=std::make_shared<std::packaged_task<void()>>([this]{if(initialized_)return;try{initialize();}catch(...){fatal_=std::current_exception();throw;}});auto future=task->get_future();{std::lock_guard lock(mutex_);tasks_.push_back([task]{(*task)();});}condition_.notify_one();return future;
}
void QueueManager::run() {
    try{initialize();}catch(...){fatal_=std::current_exception();}
    for(;;) {
        std::deque<std::function<void()>> tasks; bool closing;
        { std::unique_lock lock(mutex_); condition_.wait_for(lock,std::chrono::milliseconds(50),[this]{return closing_ || !tasks_.empty();}); tasks.swap(tasks_); closing=closing_; }
        for(auto& task:tasks) task();
        if(closing) break;
        if(!fatal_) try { tick(); schedule(); } catch(...) { fatal_=std::current_exception(); for(auto& [id,w]:workers_) w->control.requestPause(); }
    }
    for(auto& [id,w]:workers_) w->control.requestPause();
    for(auto& [id,w]:workers_) if(w->thread.joinable()) w->thread.join();
    if(!fatal_) try { tick();store_->backup(); } catch(...) {}
    workers_.clear();store_.reset();lease_.reset(); // Release database before releasing process lease.
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
void QueueManager::cleanup(QueueEntry& e) {
    if(e.status==QueueStatus::Completed){
        if(e.media&&e.mediaStaged)ownedMedia::erase(path(ownedMedia::workspace(e.mediaFolder,e.id)),0,true,e.mediaIdentity);
        else if(!e.media&&!e.browserTransfer){ResumeJournal journal(path(e.destination));journal.discardPartial(e.url.toStdString());}
        return;
    }
    if(e.media){
        if(e.mediaStaged){const auto stage=ownedMedia::workspace(e.mediaFolder,e.id);ordinaryParents(path(stage));if(e.mediaIdentity.isEmpty())invalid("Video staging ownership is unavailable; the folder was retained.");ownedMedia::erase(path(stage),0,true,e.mediaIdentity);}
    } else if(e.browserTransfer){
        if(QFileInfo::exists(e.destination+".part")){
            if(e.partialIdentity.isEmpty())invalid("This older partial has no ownership identity. It was retained for manual review.");
            ordinaryParents(path(e.destination));FileWriter::discardOwnedPartial(path(e.destination),e.partialIdentity.toStdString());e.partialIdentity.clear();
        }
    } else {
        ordinaryParents(path(e.destination));ResumeJournal journal(path(e.destination));journal.discardPartial(e.url.toStdString());
    }
    e.progress.downloadedBytes=e.progress.verifiedBytes=0;e.progress.resumable=false;e.existing=ExistingDownload::New;
}
std::future<QString> QueueManager::add(QString url,QString dest,int priority,bool start,QString sha256,std::uint64_t speedLimit) {
    return submit([this,url=std::move(url),dest=std::move(dest),priority,start,sha256=std::move(sha256),speedLimit] {
        if(youtubeHost(QUrl(url).host())) invalid("This is a YouTube page, not a file. Use Download video to resolve its streams and qualities.");
        if(entries_.size()>=10000 || priority < -1 || priority > 1 || speedLimit > 10ULL*1024*1024*1024) invalid("Queue limit or invalid download options.");
        QueueEntry e; e.url=url; e.origin=origin(url); e.destination=destination(dest); e.sha256=sha256.toLower();
        const auto parent=QFileInfo(e.destination).absolutePath();
        for(const auto& category:QStringList{"Videos","Audios","Zip","Programs","Pictures"})if(parent.compare(QDir(settings_.directory).filePath(category),Qt::CaseInsensitive)==0 && !QDir().mkpath(parent))invalid("The category folder could not be created.");
        if(!e.sha256.isEmpty() && (e.sha256.size()!=64 || std::any_of(e.sha256.begin(),e.sha256.end(),[](QChar c){return !QString("0123456789abcdef").contains(c);}))) invalid("SHA-256 must contain 64 hexadecimal characters.");
        for(const auto& old:entries_) if(old.destination.compare(e.destination,Qt::CaseInsensitive)==0) invalid("A download already owns this destination. Choose a different filename.");
        if(QFileInfo::exists(e.destination) || QFileInfo::exists(e.destination+".part") || QFileInfo::exists(e.destination+".part.resume")) invalid("The destination or its partial already exists. Choose a different filename.");
        e.id=QString::fromStdString(ResumeJournal::newId()); e.priority=priority; e.speedLimit=speedLimit; e.created=QDateTime::currentSecsSinceEpoch(); e.status=start ? QueueStatus::Queued : QueueStatus::Paused;
        save(e); entries_.push_back(e); return e.id;
    });
}
std::future<void> QueueManager::action(QString id,QueueAction action) { return submit([this,id=std::move(id),action] {
    auto& e=entry(id); const auto worker=workers_.find(id);
    if(e.browserTransfer && e.status==QueueStatus::Cancelled && !e.browserComplete && (action==QueueAction::Start || action==QueueAction::Restart))invalid("This response was cancelled before it finished. Start the download again from its original page.");
    if(!e.media && !e.browserTransfer && !youtubeVideo(e.url).isEmpty() && (action==QueueAction::Start || action==QueueAction::Restart)) invalid("Use Retry video to download the streams, rather than the YouTube webpage.");
    if(e.media && (action!=QueueAction::Remove || e.status==QueueStatus::Running)) invalid("Use the video window to stop this task, or Retry video to retry it.");
    if(e.status==QueueStatus::Prepared) invalid("Chrome still owns this handoff. Leave it in Chrome or cancel the pending handoff.");
    if(!e.handoffKey.isEmpty() && receipts_.at(e.handoffKey).value("state")=="aborted" && (action==QueueAction::Start || action==QueueAction::Restart)) invalid("This browser handoff was aborted. Add a fresh download link instead.");
    if(action==QueueAction::HighPriority || action==QueueAction::NormalPriority || action==QueueAction::LowPriority) { e.priority=action==QueueAction::HighPriority ? 1 : action==QueueAction::LowPriority ? -1 : 0; save(e); return; }
    if(worker!=workers_.end()) {
        if(e.browserTransfer){
            if(action==QueueAction::Pause || action==QueueAction::Start){worker->second->browserPaused=action==QueueAction::Pause;e.status=action==QueueAction::Pause ? QueueStatus::Paused:QueueStatus::Running;e.error.clear();save(e);return;}
            if(action==QueueAction::Cancel){worker->second->cancel=true;worker->second->thread.request_stop();return;}
            invalid("Pause or cancel this browser transfer before changing it.");
        }
        if(action==QueueAction::Pause) worker->second->control.requestPause();
        else if(action==QueueAction::Cancel) { worker->second->cancel=true; worker->second->thread.request_stop(); }
        else if(action!=QueueAction::Start) invalid("Pause the download before changing or removing it.");
        return;
    }
    if(action==QueueAction::Remove) { cleanup(e);store_->erase(id); std::erase_if(entries_,[&](const auto& row){return row.id==id;}); return; }
    if(e.status==QueueStatus::Completed) invalid("This download is already complete.");
    if(action==QueueAction::Start && e.status==QueueStatus::Paused && e.error.startsWith("Download paused") && e.progress.resumable && e.progress.downloadedBytes && !e.progress.verifiedBytes && QFileInfo::exists(e.destination+".part.resume")) {
        ResumeJournal journal(path(e.destination));
        if(journal.load().completed.empty())invalid("This earlier pause has no reusable checkpoint. Choose Restart explicitly; the old partial is preserved.");
    }
    if(action==QueueAction::Pause) e.status=QueueStatus::Paused;
    else if(action==QueueAction::Cancel) {cleanup(e);e.status=QueueStatus::Cancelled;e.error="Cancelled. Incomplete data removed.";}
    else {if(action==QueueAction::Restart){cleanup(e);e.progress={};} e.existing=action==QueueAction::Restart ? ExistingDownload::New : QFileInfo::exists(e.destination+".part.resume") ? ExistingDownload::Resume : ExistingDownload::New; e.status=QueueStatus::Queued; e.error.clear(); }
    save(e);
}); }
std::future<void> QueueManager::rename(QString id,QString dest) { return submit([this,id=std::move(id),dest=std::move(dest)] {
    auto& e=entry(id); if(e.media || workers_.contains(id) || e.status==QueueStatus::Completed || QFileInfo::exists(e.destination+".part")) invalid("Only an unstarted file download can be renamed.");
    const auto d=destination(dest);if(e.browserTransfer && !e.browserFile.isEmpty() && d.compare(e.browserFile,Qt::CaseInsensitive)==0)invalid("Choose a destination separate from the incoming browser file."); for(const auto& old:entries_) if(old.id!=id && old.destination.compare(d,Qt::CaseInsensitive)==0) invalid("Another download owns that destination.");
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
std::future<QueueSnapshot> QueueManager::snapshot() { return submit([this] { tick(); return QueueSnapshot{entries_,settings_,backupWarning_}; }); }
std::future<QString> QueueManager::categoryFolder(QString filename,QString mime) {return submit([this,filename=std::move(filename),mime=std::move(mime)]{const auto folder=QDir(settings_.directory).filePath(downloadCategory(filename,mime));if(!QDir().mkpath(folder))invalid("The download category folder could not be created.");return folder;});}
std::future<QString> QueueManager::beginMedia(QString url,QString folder,QString title,QJsonObject metadata) {
    return submit([this,url=std::move(url),folder=std::move(folder),title=std::move(title),metadata=std::move(metadata)] {
        if(entries_.size()>=10000 || !QFileInfo(folder).isDir()) invalid("Select an existing video folder, or remove old history if it is full.");
        QueueEntry e;e.url=url;e.origin=origin(url);e.media=true;e.mediaTitle=title.left(300);
        e.mediaFolder=QFileInfo(folder).canonicalFilePath();e.destination=e.mediaFolder;
        for(const auto& old:entries_)if(old.media && (old.status==QueueStatus::Running||(old.status==QueueStatus::Paused&&validMediaResume(old.videoMetadata.value("resumeRecipe").toObject()))) && old.url==url && old.mediaFolder.compare(e.mediaFolder,Qt::CaseInsensitive)==0)invalid("This video already has an active or paused task in that folder. Resume it from the list.");
        e.id=QString::fromStdString(ResumeJournal::newId());e.mediaStaged=true;
        if(metadata.contains("requestContext") && (!metadata.value("requestContext").isObject() || !validMediaContext(metadata.value("requestContext").toObject())))invalid("Invalid video request context.");
        const auto browser=metadata.value("requestContext").toObject().value("youtube").toObject();if(!browser.isEmpty()&&youtubeVideo(url)!="https://www.youtube.com/watch?v="+browser.value("id").toString())invalid("The browser source does not match this video.");
        if(metadata.contains("resumeRecipe")&&!validMediaResume(metadata.value("resumeRecipe").toObject()))invalid("Invalid video resume selection.");
        if(metadata.contains("browserSource") && (!metadata.value("browserSource").isBool() || (metadata.value("browserSource").toBool()&&browser.isEmpty())))invalid("Invalid video source selection.");
        for(const auto& field:QStringList{"title","thumbnail","originalPage","webpage_url","extractor_key","duration","height","requestContext","downloadName","resumeRecipe","browserSource"})if(metadata.contains(field))e.videoMetadata.insert(field,metadata.value(field));
        if(QJsonDocument(e.videoMetadata).toJson(QJsonDocument::Compact).size()>65536)invalid("Video preview metadata exceeds its limit.");
        const auto stage=ownedMedia::workspace(e.mediaFolder,e.id);ordinaryParents(path(stage));if(!QDir().mkdir(stage))invalid("Cannot create a private video staging folder.");e.mediaIdentity=ownedMedia::identity(stage);
        e.status=QueueStatus::Running;e.created=QDateTime::currentSecsSinceEpoch();save(e);entries_.push_back(e);return e.id;
    });
}
std::future<void> QueueManager::mediaProgress(QString id,DownloadProgress progress) {
    return submit([this,id=std::move(id),progress] {auto& e=entry(id);if(e.media && e.status==QueueStatus::Running)e.progress=progress;});
}
std::future<void> QueueManager::pauseMedia(QString id,bool paused,QJsonObject context) {return submit([this,id=std::move(id),paused,context=std::move(context)]{
    auto& e=entry(id);if(!e.media||!e.mediaStaged||!validMediaResume(e.videoMetadata.value("resumeRecipe").toObject())||e.status!=(paused?QueueStatus::Running:QueueStatus::Paused))invalid("This video cannot change its pause state.");
    auto metadata=e.videoMetadata;if(!context.isEmpty()){
        if(paused || !validMediaContext(context) || (context.contains("youtube") && youtubeVideo(e.url)!="https://www.youtube.com/watch?v="+context.value("youtube").toObject().value("id").toString()))invalid("Invalid refreshed video source.");
        metadata["requestContext"]=context;if(QJsonDocument(metadata).toJson(QJsonDocument::Compact).size()>65536)invalid("Video preview metadata exceeds its limit.");
    }
    const auto stage=ownedMedia::workspace(e.mediaFolder,e.id);ordinaryParents(path(stage));if(e.mediaIdentity.isEmpty()||ownedMedia::identity(stage)!=e.mediaIdentity)invalid("Video staging ownership changed. Existing files were retained.");
    e.videoMetadata=metadata;
    e.status=paused?QueueStatus::Paused:QueueStatus::Running;e.error.clear();e.progress.smoothedBytesPerSecond=e.progress.instantaneousBytesPerSecond=0;e.progress.activeRanges=0;e.progress.etaSeconds.reset();e.progress.resumable=true;save(e);
});}
std::future<QString> QueueManager::finishMedia(QString id,QueueStatus status,QString file,QString error,QJsonObject metadata) {
    return submit([this,id=std::move(id),status,file=std::move(file),error=std::move(error),metadata=std::move(metadata)] {
        auto& e=entry(id);if(!e.media || (e.status!=QueueStatus::Running&&e.status!=QueueStatus::Paused))invalid("This video task is no longer active.");
        if(status!=QueueStatus::Completed && status!=QueueStatus::Failed && status!=QueueStatus::Cancelled)invalid("Invalid video result.");
        if(status==QueueStatus::Completed&&e.status==QueueStatus::Paused)invalid("Resume this video before completing it.");
        if(status==QueueStatus::Completed) {
            for(const auto& field:QStringList{"title","thumbnail","webpage_url","duration","height","ext"})if(metadata.contains(field))e.videoMetadata[field]=metadata.value(field);
            if(QJsonDocument(e.videoMetadata).toJson(QJsonDocument::Compact).size()>65536)invalid("Video preview metadata exceeds its limit.");
            // Completion is durable only for a nonempty, ordinary file in the chosen folder.
            // The asynchronous ffprobe stage in MediaDialog verifies playable video first.
            const auto d=destination(file);const QFileInfo info(d);
            if(e.mediaStaged&&ownedMedia::identity(ownedMedia::workspace(e.mediaFolder,e.id))!=e.mediaIdentity)invalid("Video staging folder was replaced. Files were retained.");
            if(!info.isAbsolute() || info.absoluteDir().canonicalPath().compare(e.mediaStaged ? ownedMedia::workspace(e.mediaFolder,e.id):e.mediaFolder,Qt::CaseInsensitive)!=0 || info.suffix()=="part")invalid("No final video was saved in the chosen folder.");
            for(const auto& old:entries_)if(!old.media && old.destination.compare(d,Qt::CaseInsensitive)==0)invalid("Another download owns this video filename.");
            const auto handle=CreateFileW(path(d).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(handle==INVALID_HANDLE_VALUE)invalid("The final video file is missing or unavailable.");
            BY_HANDLE_FILE_INFORMATION attributes{};LARGE_INTEGER size{};
            const bool valid=GetFileInformationByHandle(handle,&attributes) && !(attributes.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && GetFileSizeEx(handle,&size) && size.QuadPart>0;
            CloseHandle(handle);if(!valid)invalid("The final video is empty or is not a regular file.");
            e.destination=e.mediaStaged ? ownedMedia::publish(d,e.mediaFolder):d;e.progress.downloadedBytes=static_cast<std::uint64_t>(size.QuadPart);e.progress.totalBytes=e.progress.downloadedBytes;
        }
        e.status=status;e.error=error;e.progress.smoothedBytesPerSecond=0;e.progress.activeRanges=0;e.progress.etaSeconds.reset();
        if(status!=QueueStatus::Completed)try{cleanup(e);}catch(const std::exception& cleanupError){e.error+=" Cleanup: "+QString::fromUtf8(cleanupError.what());save(e);throw;}
        else if(e.mediaStaged)try{ownedMedia::erase(path(ownedMedia::workspace(e.mediaFolder,e.id)),0,true,e.mediaIdentity);}catch(...){e.error="Video completed; empty staging folder cleanup needs retry.";}
        save(e);return e.destination;
    });
}
void QueueManager::tick() {
    for(auto i=workers_.begin();i!=workers_.end();) {
        auto& w=*i->second; auto& e=entry(i->first); bool done;
        { std::lock_guard lock(w.mutex); e.progress=w.progress; done=w.done; if(done) { e.status=w.cancel && w.status!=QueueStatus::Completed ? QueueStatus::Cancelled : w.status; e.error=w.error; } }
        if(done) {if(w.thread.joinable())w.thread.join();if(!w.partialIdentity.isEmpty())e.partialIdentity=w.partialIdentity;
            if(e.status==QueueStatus::Failed || e.status==QueueStatus::Cancelled)try{cleanup(e);if(e.status==QueueStatus::Cancelled)e.error="Cancelled. Incomplete data removed.";else e.error+=" Incomplete data removed.";}catch(const std::exception& error){e.error+=QString(" Cleanup: ")+QString::fromUtf8(error.what());}
            e.progress.activeRanges=0;e.progress.smoothedBytesPerSecond=e.progress.instantaneousBytesPerSecond=0;e.progress.etaSeconds.reset();save(e);if(e.status==QueueStatus::Paused)try{store_->backup();backupWarning_.clear();}catch(const std::exception& error){backupWarning_=QString::fromUtf8(error.what());} i=workers_.erase(i); } else ++i;
    }
}
void QueueManager::schedule() {
    // A fully received response is local and can finish after restart even if
    // its browser tab/worker is no longer available.
    for(const auto& e:entries_)if(e.browserTransfer && e.browserComplete && e.status==QueueStatus::Queued && !e.browserFile.isEmpty() && e.requiredTotal)
        (void)browserUpdate(e.handoffKey,e.browserFile,true,static_cast<qint64>(*e.requiredTotal),static_cast<qint64>(*e.requiredTotal));
    std::vector<QueueEntry*> pending; for(auto& e:entries_) if(!e.media && !e.browserTransfer && e.status==QueueStatus::Queued) pending.push_back(&e);
    std::stable_sort(pending.begin(),pending.end(),[](auto* a,auto* b){return a->priority>b->priority;});
    for(auto* e:pending) {
        if(!youtubeVideo(e->url).isEmpty()){e->status=QueueStatus::Failed;e->error="This is a YouTube webpage. Choose Retry video to download its streams.";save(*e);continue;}
        if(static_cast<std::size_t>(std::count_if(workers_.begin(),workers_.end(),[this](const auto& pair){return entry(pair.first).status==QueueStatus::Running;}))>=static_cast<std::size_t>(settings_.simultaneous)) break;
        int host=0; for(const auto& [id,w]:workers_) if(entry(id).origin==e->origin) ++host;
        if(host>=std::min(settings_.perHost,settings_.hostConnections)) continue;
        auto w=std::make_unique<Worker>(); auto* mailbox=w.get();
        if(e->existing==ExistingDownload::Resume){w->progress=e->progress;w->progress.stage=DownloadStage::Probe;w->progress.activeRanges=0;w->progress.smoothedBytesPerSecond=w->progress.instantaneousBytesPerSecond=0;}
        DownloadOptions options;options.discardIncomplete=true; options.url=e->url.toStdString(); options.destination=path(e->destination); options.existing=e->existing; options.control=&mailbox->control;
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
            catch(...) { status=QueueStatus::Failed; error="The download worker failed. Incomplete data cleanup will be attempted."; }
            { std::lock_guard lock(mailbox->mutex); mailbox->status=status; mailbox->error=error; mailbox->done=true; }
        }); } catch(...) { workers_.erase(id); e->status=QueueStatus::Failed; e->error="Unable to create a download worker."; save(*e); }
    }
}
std::future<QJsonObject> QueueManager::prepare(QString key,QString url,QString filename,DownloadEngine::Inspection inspection,QString mime,bool browserTransfer) {
    return submit([this,key=std::move(key),url=std::move(url),filename=std::move(filename),inspection=std::move(inspection),mime=std::move(mime),browserTransfer] {
        if(!browserTransfer && youtubeHost(QUrl(url).host()))invalid("Use Download video for YouTube rather than saving its watch page.");
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(),QCryptographicHash::Sha256).toHex());
        if(const auto i=receipts_.find(key);i!=receipts_.end()) {if(i->second.value("urlHash")!=hash)invalid("This handoff key belongs to another request.");return QJsonObject{{"state",i->second.value("state")},{"downloadId",i->second.value("downloadId")}};}
        if(entries_.size()>=10000 || receipts_.size()>=10000)invalid("The browser handoff/history limit has been reached.");
        if(key.size()!=32 || std::any_of(key.begin(),key.end(),[](QChar c){return !QString("0123456789abcdef").contains(c);}))invalid("Invalid handoff key.");
        if(filename.isEmpty() || filename.size()>160 || filename.contains('/') || filename.contains('\\') || filename=="." || filename=="..")invalid("Invalid browser filename.");
        QueueEntry e;e.id=QString::fromStdString(ResumeJournal::newId());e.url=url;e.browserTransfer=browserTransfer;e.origin=browserTransfer ? browserOrigin(url):origin(url);e.handoffKey=key;e.status=QueueStatus::Prepared;e.created=QDateTime::currentSecsSinceEpoch();e.updated=e.created;
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
    const auto e=std::find_if(entries_.begin(),entries_.end(),[&](const auto& row){return row.id==id;});result["downloadStatus"]=e==entries_.end() ? "Removed":statusText(e->status);result["browserTransfer"]=e!=entries_.end() && e->browserTransfer;return result;
});}
std::future<QJsonObject> QueueManager::browserUpdate(QString key,QString file,bool complete,qint64 size,qint64 received,QString sourceError) {
    return submit([this,key=std::move(key),file=std::move(file),complete,size,received,sourceError=std::move(sourceError)] {
        tick();
        const auto receipt=receipts_.find(key);if(receipt==receipts_.end())invalid("Unknown browser transfer.");
        const auto id=receipt->second.value("downloadId").toString();
        const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.id==id;});
        if(found==entries_.end())return QJsonObject{{"downloadStatus","Removed"}};
        auto& e=*found;if(!e.browserTransfer)invalid("This entry uses the direct download engine.");
        if(complete && (!e.browserComplete || e.requiredTotal!=static_cast<std::uint64_t>(size))){e.browserComplete=true;e.requiredTotal=static_cast<std::uint64_t>(size);save(e);}
        if(!file.isEmpty()) {
            if(!QFileInfo(file).isAbsolute() || file.size()>32768 || file.contains(QChar(0)))invalid("Invalid browser transfer path.");
            const auto checked=destination(file);ordinaryParents(path(checked));
            if(checked.compare(e.destination,Qt::CaseInsensitive)==0 || (checked+".crdownload").compare(e.destination+".part",Qt::CaseInsensitive)==0)invalid("Browser source and destination must be separate files.");
            if(!e.browserFile.isEmpty() && e.browserFile.compare(checked,Qt::CaseInsensitive)!=0)invalid("The browser transfer path changed. Existing bytes are preserved.");
            if(e.browserFile.isEmpty()){e.browserFile=checked;save(e);}
        }
        if(auto i=workers_.find(id);i!=workers_.end()) {
            if(!i->second->browser)invalid("The transfer worker does not match this entry.");
            i->second->browserReceived=received;
            if(!sourceError.isEmpty()){{std::lock_guard lock(i->second->mutex);i->second->error="Transfer interrupted: "+sourceError;i->second->browserFailed=true;}}
            if(complete){i->second->browserSize=size;i->second->browserComplete=true;}
        } else if(!sourceError.isEmpty() && e.status!=QueueStatus::Completed && e.status!=QueueStatus::Cancelled){e.status=QueueStatus::Failed;e.error="Transfer interrupted: "+sourceError;try{cleanup(e);}catch(const std::exception& error){e.error+=" Cleanup: "+QString::fromUtf8(error.what());}save(e);
        } else if(e.status==QueueStatus::Queued && !e.browserFile.isEmpty() && static_cast<std::size_t>(std::count_if(workers_.begin(),workers_.end(),[this](const auto& pair){return entry(pair.first).status==QueueStatus::Running;}))<static_cast<std::size_t>(settings_.simultaneous)) {
            auto worker=std::make_unique<Worker>();auto* w=worker.get();w->browser=true;w->browserComplete=complete;w->browserSize=size;w->browserReceived=received;w->progress=e.progress;w->progress.resumable=true;
            const auto source=path(e.browserFile),target=path(e.destination);const auto ownedIdentity=e.partialIdentity;ordinaryParents(target);
            e.status=QueueStatus::Running;e.error.clear();save(e);workers_.emplace(id,std::move(worker));
            try{w->thread=std::jthread([w,source,target,ownedIdentity](std::stop_token stop){
                struct Input {HANDLE handle=INVALID_HANDLE_VALUE;~Input(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} input;
                std::unique_ptr<FileWriter> output;
                QueueStatus result=QueueStatus::Completed;QString error;
                try {
                    const auto interrupted=[&]{if(w->browserFailed){std::lock_guard lock(w->mutex);throw DownloadError(DownloadErrorCode::Network,w->error.toStdString());}if(stop.stop_requested() || w->control.pauseRequested())throw DownloadError(DownloadErrorCode::Paused,"Transfer interrupted. Saved bytes are preserved; choose Resume.");};
                    // Chrome may preallocate or write parallel ranges out of order.
                    // Never mistake file length/bytesReceived for a verified prefix.
                    const auto receiving=std::chrono::steady_clock::now();
                    while(!w->browserComplete){
                        interrupted();const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-receiving).count();
                        {std::lock_guard lock(w->mutex);auto& p=w->progress;p.downloadedBytes=static_cast<std::uint64_t>(w->browserReceived.load());p.elapsedSeconds=elapsed;p.smoothedBytesPerSecond=p.averageBytesPerSecond=elapsed>0 ? static_cast<double>(p.downloadedBytes)/elapsed:0;if(w->browserSize>=0)p.totalBytes=static_cast<std::uint64_t>(w->browserSize.load());p.activeRanges=w->browserPaused ? 0:1;}
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    }
                    while(input.handle==INVALID_HANDLE_VALUE){
                        interrupted();ordinaryParents(source);
                        auto current=source;current+=L".crdownload";
                        for(const auto& name:{current,source}){
                            input.handle=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                            if(input.handle!=INVALID_HANDLE_VALUE)break;
                        }
                        if(input.handle==INVALID_HANDLE_VALUE){if(w->browserComplete)invalid("The received browser file is missing or cannot be read. Check its storage location.");std::this_thread::sleep_for(std::chrono::milliseconds(50));}
                    }
                    BY_HANDLE_FILE_INFORMATION info{};
                    if(!GetFileInformationByHandle(input.handle,&info) || info.nNumberOfLinks!=1 || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))invalid("The browser source must be an ordinary file without links.");
                    const bool reopening=std::filesystem::exists(target.wstring()+L".part");output=std::make_unique<FileWriter>(target,reopening);
                    const auto identity=QString::fromStdString(output->identity());if(!reopening||identity==ownedIdentity)w->partialIdentity=identity;
                    if(reopening&&!ownedIdentity.isEmpty()&&identity!=ownedIdentity)invalid("The browser partial was replaced. It was retained for review.");
                    auto copied=output->size();const auto recovered=copied;
                    QByteArray buffer(1024*1024,Qt::Uninitialized);QCryptographicHash received(QCryptographicHash::Sha256);
                    const auto sourceSize=[&]{LARGE_INTEGER bytes{};if(!GetFileSizeEx(input.handle,&bytes) || bytes.QuadPart<0)invalid("Cannot read the received file size.");return static_cast<std::uint64_t>(bytes.QuadPart);};
                    const auto read=[&](DWORD count){DWORD n=0;if(!ReadFile(input.handle,buffer.data(),count,&n,nullptr))invalid("Reading the browser transfer failed.");return n;};
                    // Reuse only a byte-for-byte matching prefix, including after app restart.
                    for(std::uint64_t offset=0;offset<copied;){interrupted();const auto n=read(static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(),copied-offset)));if(!n)invalid("The browser source is shorter than the saved partial. Existing bytes are preserved.");received.addData(QByteArrayView(buffer.constData(),n));offset+=n;}
                    if(copied && QString::fromStdString(output->sha256())!=QString::fromLatin1(received.result().toHex()))invalid("The browser source changed. Existing partial bytes are preserved.");
                    w->partialIdentity=identity;const auto started=std::chrono::steady_clock::now();auto checkpoint=copied;
                    for(;;){
                        interrupted();
                        if(w->browserPaused){output->flush();while(w->browserPaused){interrupted();std::this_thread::sleep_for(std::chrono::milliseconds(30));}}
                        const auto available=sourceSize();if(available<copied)invalid("The browser source was replaced or truncated during transfer.");
                        const auto n=read(static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(),available-copied)));
                        if(n){output->writeAt(copied,buffer.constData(),n);received.addData(QByteArrayView(buffer.constData(),n));copied+=n;if(copied-checkpoint>=4*1024*1024){output->flush();checkpoint=copied;}}
                        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
                        {std::lock_guard lock(w->mutex);auto& p=w->progress;p.stage=DownloadStage::Finalizing;p.downloadedBytes=std::max(copied,static_cast<std::uint64_t>(w->browserReceived.load()));p.recoveredBytes=recovered;p.resumable=true;p.elapsedSeconds=elapsed;p.averageBytesPerSecond=elapsed>0 ? static_cast<double>(copied-recovered)/elapsed:0;p.smoothedBytesPerSecond=p.instantaneousBytesPerSecond=0;p.activeRanges=1;p.targetConnections=1;if(w->browserSize>=0)p.totalBytes=static_cast<std::uint64_t>(w->browserSize.load());}
                        if(w->browserComplete && copied==available){
                            if(w->browserSize<0 || copied!=static_cast<std::uint64_t>(w->browserSize.load()))invalid("Received browser bytes do not match the completed file size.");
                            const auto expected=received.result().toHex();LARGE_INTEGER beginning{};if(!SetFilePointerEx(input.handle,beginning,nullptr,FILE_BEGIN))invalid("Cannot verify the browser source.");
                            QCryptographicHash finalSource(QCryptographicHash::Sha256);std::uint64_t verified=0;
                            while(verified<copied){interrupted();const auto count=read(static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(),copied-verified)));if(!count)invalid("The received file changed before verification.");finalSource.addData(QByteArrayView(buffer.constData(),count));verified+=count;}
                            if(sourceSize()!=copied || finalSource.result().toHex()!=expected)invalid("The received file changed during transfer. Partial bytes are preserved.");
                            output->commit(copied,expected.toStdString());
                            {std::lock_guard lock(w->mutex);w->progress.stage=DownloadStage::Completed;w->progress.downloadedBytes=w->progress.verifiedBytes=copied;w->progress.totalBytes=copied;}
                            break;
                        }
                        if(!n)std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    }
                }catch(const DownloadError& e){result=e.code()==DownloadErrorCode::Paused ? QueueStatus::Paused:QueueStatus::Failed;error=QString::fromUtf8(e.what());}
                catch(const std::exception& e){result=QueueStatus::Failed;error=QString::fromUtf8(e.what());}
                try{if(output && result!=QueueStatus::Completed)output->flush();}catch(...){}
                output.reset();
                {std::lock_guard lock(w->mutex);w->status=result;w->error=error;w->done=true;}
            });}catch(...){workers_.erase(id);e.status=QueueStatus::Failed;e.error="Cannot start the browser transfer worker.";save(e);}
        }
        return QJsonObject{{"downloadStatus",statusText(e.status)},{"downloadId",e.id},{"bytes",QString::number(e.progress.downloadedBytes)}};
    });
}
}
