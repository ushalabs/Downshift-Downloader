#pragma once
#include <QObject>
#include <QString>
#include <QMetaType>
#include <future>
#include <memory>
#include <vector>

namespace downshift::torrent {
enum class State { Adding, Metadata, Checking, Downloading, Complete, Failed, Cancelled };
struct File { QString path; qint64 bytes=0; };
struct Snapshot {
    QString id, name, directory, error;
    State state=State::Adding;
    bool metadata=false;
    qint64 totalBytes=0, verifiedBytes=0;
    int downloadBytesPerSecond=0, uploadBytesPerSecond=0, peers=0;
    std::vector<File> files;
};
struct Options {
    bool discovery=true;
    // Used by controlled local-peer tests; production listens on available interfaces.
    bool loopbackOnly=false;
};

// Commands and metadata parsing run on an owned worker. Never wait on futures on
// the GUI thread. Signals carry copies, not libtorrent alert/handle pointers.
class TorrentEngine final : public QObject {
    Q_OBJECT
public:
    explicit TorrentEngine(Options options={},QObject* parent=nullptr);
    ~TorrentEngine() override;
    std::future<QString> addFile(QString file,QString directory);
    std::future<QString> addMagnet(QString uri,QString directory);
    std::future<std::vector<Snapshot>> snapshots();
    std::future<void> cancel(QString id); // Retains all data; metadata cancellation in T1.
    std::future<void> connectPeer(QString id,QString address,quint16 port);
    // Begin asynchronous teardown; await outside the GUI thread before destruction.
    std::shared_future<void> shutdown();
signals:
    void updated(downshift::torrent::Snapshot snapshot);
    void engineFailed(QString message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
Q_DECLARE_METATYPE(downshift::torrent::Snapshot)
