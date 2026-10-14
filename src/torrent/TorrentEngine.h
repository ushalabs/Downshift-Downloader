#pragma once
#include <QObject>
#include <QString>
#include <QMetaType>
#include <future>
#include <memory>
#include <vector>

namespace downshift::torrent {
enum class State { Adding, Metadata, Checking, Downloading, Complete, Failed, Cancelled, Paused, Seeding };
enum class SeedMode { Stop, Ratio, Time, Manual };
struct Limits { int download=0, upload=256*1024, connections=64; };
struct SeedPolicy { SeedMode mode=SeedMode::Stop; double ratio=1.0; int seconds=3600; };
struct AddOptions { bool paused=false; std::vector<int> priorities; Limits limits; SeedPolicy seeding; };
struct File { QString path; qint64 bytes=0, verifiedBytes=0; int index=0, priority=4; };
struct Peer { QString address, client; int download=0, upload=0; bool seed=false; };
struct Tracker { QString url, error; bool verified=false; int failures=0; };
struct Details { std::vector<Peer> peers; std::vector<Tracker> trackers; std::vector<int> availability; };
struct Snapshot {
    QString id, name, directory, error;
    State state=State::Adding;
    bool metadata=false;
    qint64 totalBytes=0, verifiedBytes=0;
    int downloadBytesPerSecond=0, uploadBytesPerSecond=0, peers=0;
    int seeds=0;
    qint64 downloadedBytes=0, uploadedBytes=0, selectedBytes=0, seedingSeconds=0;
    double ratio=0, availability=0;
    Limits limits;
    SeedPolicy seeding;
    std::vector<File> files;
};
struct Options {
    bool discovery=true;
    // Used by controlled local-peer tests; production listens on available interfaces.
    bool loopbackOnly=false;
    // Empty is ephemeral. T2 uses an explicitly selected companion SQLite store;
    // never pass the existing HTTP queue.sqlite to this independent subsystem.
    QString databasePath;
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
    std::future<QString> addFile(QString file,QString directory,AddOptions options);
    std::future<QString> addMagnet(QString uri,QString directory,AddOptions options);
    std::future<std::vector<Snapshot>> snapshots();
    std::future<void> pause(QString id);
    std::future<void> resume(QString id);
    std::future<void> checkpoint(QString id);
    std::future<void> configure(QString id,Limits limits,SeedPolicy seeding);
    // Priorities correspond to ordinary files in Snapshot.files (0 skip, 1..7).
    std::future<void> selectFiles(QString id,std::vector<int> priorities);
    std::future<void> setSessionLimits(Limits limits);
    std::future<Limits> sessionLimits();
    std::future<Details> details(QString id);
    std::future<quint16> listeningPort();
    std::future<void> cancel(QString id); // Durable cancellation; retains all data.
    std::future<void> remove(QString id,bool deleteData=false,bool confirmed=false);
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
