#pragma once
#include "core/DownloadQueue/QueueManager.h"
#include <atomic>
namespace downshift::browser {
class PipeServer final {
public:
    PipeServer(QueueManager& queue,std::function<void()> open,std::function<void(QString,QString,QJsonObject)> media={},std::function<void()> blocked={},std::function<void(QString)> torrent={});
    ~PipeServer();
    bool active() const {return active_.load();}
private:
    void run(std::stop_token stop);
    QJsonObject handle(const QJsonObject& request,std::stop_token stop);
    QueueManager& queue_;
    std::function<void()> open_;
    std::function<void(QString,QString,QJsonObject)> media_;
    std::function<void()> blocked_;
    std::function<void(QString)> torrent_;
    std::atomic_bool active_=false;
    std::jthread thread_;
};
}
