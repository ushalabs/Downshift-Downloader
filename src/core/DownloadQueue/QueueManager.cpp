#include "QueueManager.h"
#include "database/QueueStore.h"
#include "core/FileWriter/FileWriter.h"
#include "core/ResumeManager/ResumeJournal.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
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
    {"downloaded",QString::number(progress.downloadedBytes)},{"verified",QString::number(progress.verifiedBytes)},{"total",progress.totalBytes ? QString::number(*progress.totalBytes) : QString()},{"resumable",progress.resumable}, {"journal",destination+".part.resume"}}; }
QueueEntry QueueEntry::fromJson(const QJsonObject& o) {
    QueueEntry e; e.id=o.value("id").toString(); e.url=o.value("url").toString(); e.destination=o.value("destination").toString(); e.origin=downshift::origin(e.url);
    e.error=o.value("error").toString(); e.sha256=o.value("sha256").toString(); e.status=static_cast<QueueStatus>(o.value("status").toInt(-1)); e.priority=o.value("priority").toInt();
    e.created=o.value("created").toString().toLongLong(); e.updated=o.value("updated").toString().toLongLong(); e.existing=static_cast<ExistingDownload>(o.value("existing").toInt(-1)); e.speedLimit=o.value("speedLimit").toString().toULongLong();
    e.progress.downloadedBytes=o.value("downloaded").toString().toULongLong(); e.progress.verifiedBytes=o.value("verified").toString().toULongLong();
    const auto total=o.value("total").toString(); if(!total.isEmpty()) e.progress.totalBytes=total.toULongLong(); e.progress.resumable=o.value("resumable").toBool();
    if(e.id.size()!=32 || e.status < QueueStatus::Queued || e.status > QueueStatus::Prepared || e.existing < ExistingDownload::New || e.existing > ExistingDownload::Restart || e.priority < -1 || e.priority > 1 || e.destination.isEmpty()) invalid("Damaged download record.");
    return e;
}
class QueueManager::RateBudget {
    std::mutex mutex; std::uint64_t rate=0;
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
public:
    void configure(std::uint64_t value) { std::lock_guard lock(mutex); rate=value; next=std::chrono::steady_clock::now(); }
    void wait(std::size_t count, std::stop_token stop, DownloadControl* control) {
        std::chrono::steady_clock::time_point due;
        { std::lock_guard lock(mutex); if (!rate) return; next=std::max(next,std::chrono::steady_clock::now()) + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(static_cast<double>(count)/static_cast<double>(rate))); due=next; }
        while (std::chrono::steady_clock::now() < due) {
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
        for(const auto& row:store_->load()) {
            auto e=QueueEntry::fromJson(object(row));
            if(e.status==QueueStatus::Running || (e.status==QueueStatus::Queued && !settings_.startOnLaunch)) { e.status=QueueStatus::Paused; e.error="Interrupted or held at startup. Choose Resume to continue."; save(e); }
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
}
std::future<QString> QueueManager::add(QString url,QString dest,int priority,bool start,QString sha256,std::uint64_t speedLimit) {
    return submit([this,url=std::move(url),dest=std::move(dest),priority,start,sha256=std::move(sha256),speedLimit] {
        if(entries_.size()>=10000 || priority < -1 || priority > 1 || speedLimit > 10ULL*1024*1024*1024) invalid("Queue limit or invalid download options.");
        QueueEntry e; e.url=url; e.origin=origin(url); e.destination=destination(dest); e.sha256=sha256.toLower();
        if(!e.sha256.isEmpty() && (e.sha256.size()!=64 || std::any_of(e.sha256.begin(),e.sha256.end(),[](QChar c){return !QString("0123456789abcdef").contains(c);}))) invalid("SHA-256 must contain 64 hexadecimal characters.");
        for(const auto& old:entries_) if(old.destination.compare(e.destination,Qt::CaseInsensitive)==0) invalid("A download already owns this destination. Choose a different filename.");
        if(QFileInfo::exists(e.destination) || QFileInfo::exists(e.destination+".part") || QFileInfo::exists(e.destination+".part.resume")) invalid("The destination or its partial already exists. Choose a different filename.");
        e.id=QString::fromStdString(ResumeJournal::newId()); e.priority=priority; e.speedLimit=speedLimit; e.created=QDateTime::currentSecsSinceEpoch(); e.status=start ? QueueStatus::Queued : QueueStatus::Paused;
        save(e); entries_.push_back(e); return e.id;
    });
}
std::future<void> QueueManager::action(QString id,QueueAction action) { return submit([this,id=std::move(id),action] {
    auto& e=entry(id); const auto worker=workers_.find(id);
    if(action==QueueAction::HighPriority || action==QueueAction::NormalPriority || action==QueueAction::LowPriority) { e.priority=action==QueueAction::HighPriority ? 1 : action==QueueAction::LowPriority ? -1 : 0; save(e); return; }
    if(worker!=workers_.end()) {
        if(action==QueueAction::Pause) worker->second->control.requestPause();
        else if(action==QueueAction::Cancel) { worker->second->cancel=true; worker->second->thread.request_stop(); }
        else if(action!=QueueAction::Start) invalid("Pause the download before changing or removing it.");
        return;
    }
    if(action==QueueAction::Remove) { store_->erase(id); std::erase_if(entries_,[&](const auto& row){return row.id==id;}); return; }
    if(e.status==QueueStatus::Completed) invalid("This download is already complete.");
    if(action==QueueAction::Pause) e.status=QueueStatus::Paused;
    else if(action==QueueAction::Cancel) e.status=QueueStatus::Cancelled;
    else { e.existing=action==QueueAction::Restart ? ExistingDownload::Restart : QFileInfo::exists(e.destination+".part.resume") ? ExistingDownload::Resume : ExistingDownload::New; e.status=QueueStatus::Queued; e.error.clear(); }
    save(e);
}); }
std::future<void> QueueManager::rename(QString id,QString dest) { return submit([this,id=std::move(id),dest=std::move(dest)] {
    auto& e=entry(id); if(workers_.contains(id) || e.status==QueueStatus::Completed || QFileInfo::exists(e.destination+".part")) invalid("Only an unstarted download can be renamed.");
    const auto d=destination(dest); for(const auto& old:entries_) if(old.id!=id && old.destination.compare(d,Qt::CaseInsensitive)==0) invalid("Another download owns that destination.");
    if(QFileInfo::exists(d) || QFileInfo::exists(d+".part") || QFileInfo::exists(d+".part.resume")) invalid("The destination already exists.");
    e.destination=d; save(e);
}); }
std::future<void> QueueManager::configure(QueueSettings s) { return submit([this,s=std::move(s)]() mutable {
    s=QueueSettings::fromJson(s.json()); if(s.directory.isEmpty() || !QFileInfo(s.directory).isDir()) invalid("Select an existing download directory.");
    store_->settings(QJsonDocument(s.json()).toJson(QJsonDocument::Compact)); settings_=s; budget_->configure(s.speedLimit);
}); }
std::future<QueueSnapshot> QueueManager::snapshot() { return submit([this] { tick(); return QueueSnapshot{entries_,settings_,{}}; }); }
void QueueManager::tick() {
    for(auto i=workers_.begin();i!=workers_.end();) {
        auto& w=*i->second; auto& e=entry(i->first); bool done;
        { std::lock_guard lock(w.mutex); e.progress=w.progress; done=w.done; if(done) { e.status=w.cancel ? QueueStatus::Cancelled : w.status; e.error=w.error; } }
        if(done) { save(e); i=workers_.erase(i); } else ++i;
    }
}
void QueueManager::schedule() {
    std::vector<QueueEntry*> pending; for(auto& e:entries_) if(e.status==QueueStatus::Queued) pending.push_back(&e);
    std::stable_sort(pending.begin(),pending.end(),[](auto* a,auto* b){return a->priority>b->priority;});
    for(auto* e:pending) {
        if(workers_.size()>=static_cast<std::size_t>(settings_.simultaneous)) break;
        int host=0; for(const auto& [id,w]:workers_) if(entry(id).origin==e->origin) ++host;
        if(host>=std::min(settings_.perHost,settings_.hostConnections)) continue;
        auto w=std::make_unique<Worker>(); auto* mailbox=w.get();
        DownloadOptions options; options.url=e->url.toStdString(); options.destination=path(e->destination); options.existing=e->existing; options.control=&mailbox->control;
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
}
