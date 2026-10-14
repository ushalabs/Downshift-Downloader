#pragma once
#include <QByteArray>
#include <QString>
#include <vector>
struct sqlite3;
namespace downshift {
// Only the queue's service thread owns this connection. Payloads are user-DPAPI encrypted.
class QueueStore final {
public:
    explicit QueueStore(const QString& path);
    ~QueueStore();
    QueueStore(const QueueStore&) = delete;
    QueueStore& operator=(const QueueStore&) = delete;
    std::vector<QByteArray> load();
    void save(const QString& id, const QByteArray& json);
    void erase(const QString& id);
    QByteArray settings();
    void settings(const QByteArray& json);
    std::vector<QByteArray> receipts();
    void handoff(const QString& id,const QByteArray& entry,const QString& key,const QByteArray& receipt);
    void backup();
private:
    sqlite3* db_ = nullptr;
    QString path_;
};
}
