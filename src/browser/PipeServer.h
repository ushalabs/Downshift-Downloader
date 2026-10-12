#pragma once
#include "core/DownloadQueue/QueueManager.h"
#include <atomic>
namespace downshift::browser {
class PipeServer final {
public:
    PipeServer(QueueManager& queue,std::function<void()> open);
    ~PipeServer();
    bool active() const {return active_.load();}
private:
    void run(std::stop_token stop);
    QJsonObject handle(const QJsonObject& request,std::stop_token stop);
    QueueManager& queue_;
    std::function<void()> open_;
    std::atomic_bool active_=false;
    std::jthread thread_;
};
}
