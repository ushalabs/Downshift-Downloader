#pragma once
#include <QByteArray>
#include <QString>
#include <QLockFile>
#include <memory>
#include <utility>
#include <vector>
struct sqlite3;
namespace downshift::torrent {
// Worker-owned, independent of the HTTP queue connection/schema/lease.
class TorrentStore final {
public:
    explicit TorrentStore(const QString& path);
    ~TorrentStore();
    TorrentStore(const TorrentStore&)=delete;
    TorrentStore& operator=(const TorrentStore&)=delete;
    std::vector<std::pair<QString,QByteArray>> load();
    void save(const QString& id,const QByteArray& record);
    void erase(const QString& id);
    QByteArray settings();
    void settings(const QByteArray& record);
    void backup();
    const QString& path() const noexcept { return path_; }
private:
    sqlite3* db_=nullptr;
    QString path_;
    std::unique_ptr<QLockFile> lease_;
    void* identityLease_=nullptr;
};
}
