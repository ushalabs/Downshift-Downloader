#pragma once
#include "core/DownloadEngine/DownloadEngine.h"
#include <QString>
#include <QJsonObject>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
namespace downshift {
class QueueStore;
enum class QueueStatus { Queued, Running, Paused, Completed, Failed, Cancelled, Prepared };
QString statusText(QueueStatus status);
struct QueueSettings {
    QString directory;
    int simultaneous = 3, perHost = 2, hostConnections = 16, connections = 16;
    bool automatic = true, dark = false, startOnLaunch = false, notifications = true, diagnostics = false;
    std::uint64_t speedLimit = 0;
    QJsonObject json() const;
    static QueueSettings fromJson(const QJsonObject&);
};
struct QueueEntry {
    QString id, url, destination, origin, error, sha256;
    QString handoffKey, requiredEtag, requiredResolvedUrl;
    std::optional<std::uint64_t> requiredTotal;
    QueueStatus status = QueueStatus::Queued;
    int priority = 0;
    std::uint64_t speedLimit = 0;
    qint64 created = 0, updated = 0;
    DownloadProgress progress;
    ExistingDownload existing = ExistingDownload::New;
    QJsonObject json() const;
    static QueueEntry fromJson(const QJsonObject&);
};
struct QueueSnapshot { std::vector<QueueEntry> entries; QueueSettings settings; QString error; };
enum class QueueAction { Start, Pause, Cancel, Restart, Remove, HighPriority, NormalPriority, LowPriority };
class QueueManager final {
public:
    explicit QueueManager(QString database);
    ~QueueManager();
    QueueManager(const QueueManager&) = delete;
    QueueManager& operator=(const QueueManager&) = delete;
    std::future<QString> add(QString url, QString destination, int priority = 0, bool start = true, QString sha256 = {}, std::uint64_t speedLimit = 0);
    std::future<void> action(QString id, QueueAction action);
    std::future<void> rename(QString id, QString destination);
    std::future<void> deleteCompleted(QString id);
    std::future<void> configure(QueueSettings settings);
    std::future<QueueSnapshot> snapshot();
    std::future<QJsonObject> prepare(QString key,QString url,QString filename,DownloadEngine::Inspection inspection);
    std::future<QJsonObject> handoff(QString key,QString command);
private:
    struct Worker;
    template<class F> auto submit(F function) -> std::future<decltype(function())> {
        using Result = decltype(function());
        auto task = std::make_shared<std::packaged_task<Result()>>([this, function = std::move(function)]() mutable -> Result {
            if (fatal_) std::rethrow_exception(fatal_);
            return function();
        });
        auto future = task->get_future();
        { std::lock_guard lock(mutex_); tasks_.push_back([task] { (*task)(); }); }
        condition_.notify_one(); return future;
    }
    void run();
    void tick();
    void schedule();
    QueueEntry& entry(const QString& id);
    void save(QueueEntry&);
    QString database_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> tasks_;
    bool closing_ = false;
    std::exception_ptr fatal_;
    std::unique_ptr<QueueStore> store_;
    std::vector<QueueEntry> entries_;
    QueueSettings settings_;
    std::map<QString, std::unique_ptr<Worker>> workers_;
    std::map<QString,QJsonObject> receipts_;
    class RateBudget;
    std::shared_ptr<RateBudget> budget_;
    std::thread service_;
};
}
