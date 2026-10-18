#include "TorrentFixture.h"
#include "gui/MainWindow.h"
#include "core/Runtime.h"
#include "TestWindows.h"
#include <QApplication>
#include <QTableWidget>
#include <QTimer>
#include <QThread>
#include <QBuffer>
#include <QImage>
#include <QSslSocket>
#include <psapi.h>
using namespace t4;
template<class T>T complete(std::future<T> future){wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;},60);if constexpr(std::is_void_v<T>)future.get();else return future.get();}
void complete(std::shared_future<void> future){wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;},60);future.get();}
int main(int argc,char** argv){
    QApplication app(argc,argv);HiddenTestWindows hidden;app.installEventFilter(&hidden);
    try{
        require(argc==6,"HTTP/HTTPS fixture URLs, checksum, report and trust bundle supplied");QImage codec(8,8,QImage::Format_RGB32);codec.fill(Qt::magenta);for(auto format:{"PNG","JPEG","WEBP"}){QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);require(codec.save(&buffer,format)&&!QImage::fromData(bytes).isNull(),"Packaged image codecs encode and decode");}require(QSslSocket::supportsSsl(),"Packaged TLS plugin available");QTemporaryDir temp;require(temp.isValid(),"Private T4 fixture");const auto root=temp.path();downshift::CurlRuntime curl;
        std::vector<QString> metadata;for(int i=0;i<8;++i)metadata.push_back(create(root,i%3==0?lt::create_torrent::v1_only:i%3==1?lt::create_torrent::v2_only:lt::create_flags_t{},QString("swarm-%1").arg(i)));
        lt::session first{lt::session_params(localSettings())},second{lt::session_params(localSettings())};std::vector<lt::torrent_handle> donors;
        for(auto* session:{&first,&second})for(const auto& file:metadata){auto p=lt::load_torrent_file(file.toUtf8().toStdString());p.save_path=QDir(root).filePath("seed").toStdString();p.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode);lt::error_code e;donors.push_back(session->add_torrent(p,e));require(!e,"Native donor accepts generated metadata");}
        wait([&]{return first.listen_port()!=0&&second.listen_port()!=0&&std::all_of(donors.begin(),donors.end(),[](auto h){return h.status().is_seeding;});},40);
        PROCESS_MEMORY_COUNTERS_EX baseline{};baseline.cb=sizeof(baseline);require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&baseline),sizeof(baseline)),"Resource baseline");DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
        const auto db=QDir(root).filePath("torrents.sqlite");std::vector<QString> ids;int peakPeers=0,peakActive=0;double gap=0;int beats=0;bool measure=false;auto last=std::chrono::steady_clock::now();QTimer heartbeat;heartbeat.setInterval(20);QObject::connect(&heartbeat,&QTimer::timeout,[&]{auto now=std::chrono::steady_clock::now();if(measure){gap=std::max(gap,std::chrono::duration<double>(now-last).count());++beats;}last=now;});heartbeat.start();
        {
            TorrentEngine engine({false,true,db});downshift::QueueManager http(QDir(root).filePath("queue.sqlite"));downshift::MainWindow window(http);window.attachTorrents(engine);window.show();wait([&]{return window.ready();});complete(engine.setSessionLimits({512*1024,128*1024,12}));
            for(int i=0;i<8;++i){AddOptions o;o.paused=true;o.limits={128*1024,64*1024,3};o.priorities={4,i%2?0:4};ids.push_back(complete(engine.addFile(metadata[i],QDir(root).filePath(QString("payload-%1").arg(i)),o)));}
            rejected([&]{complete(engine.addFile(metadata[0],QDir(root).filePath("duplicate"),{true}));},"Duplicate torrent rejected with eight retained records");
            const auto httpPath=QDir(root).filePath("http.bin");complete(http.add(QString::fromLocal8Bit(argv[1]),httpPath,0,true,QString::fromLatin1(argv[2]),256*1024));downshift::DownloadOptions tls;tls.url=argv[4];tls.destination=std::filesystem::path(QDir(root).filePath("https.bin").toStdWString());tls.caBundle=std::filesystem::path(QString::fromLocal8Bit(argv[5]).toStdWString());tls.expectedSha256=std::string(argv[2]);auto https=std::async(std::launch::async,[tls]{return downshift::DownloadEngine().download(tls);});measure=true;last=std::chrono::steady_clock::now();
            for(auto id:ids){complete(engine.resume(id));complete(engine.connectPeer(id,"127.0.0.1",first.listen_port()));complete(engine.connectPeer(id,"127.0.0.1",second.listen_port()));}
            wait([&]{auto rows=complete(engine.snapshots());int peers=0,active=0;for(auto r:rows){peers+=r.peers;if(r.state==State::Downloading)++active;}peakPeers=std::max(peakPeers,peers);peakActive=std::max(peakActive,active);return std::count_if(rows.begin(),rows.end(),[](auto r){return r.verifiedBytes>0;})>=4;},45);
            std::vector<std::future<void>> pauses;for(auto id:ids)pauses.push_back(engine.pause(id));for(auto& f:pauses)complete(std::move(f));for(auto r:complete(engine.snapshots()))require(r.state==State::Paused,"Concurrent pause is durable");
            // Resume with all old peers disconnected; no peer data may be invented.
            first.pause();second.pause();for(auto id:ids)complete(engine.resume(id));const auto outageUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(600);wait([&]{return std::chrono::steady_clock::now()>=outageUntil;});for(auto r:complete(engine.snapshots()))require(r.state!=State::Failed,"Peer outage is waiting, not a disk failure");first.resume();second.resume();
            for(auto id:ids){complete(engine.connectPeer(id,"127.0.0.1",first.listen_port()));complete(engine.connectPeer(id,"127.0.0.1",second.listen_port()));}
            wait([&]{auto rows=complete(engine.snapshots());int peers=0,active=0;for(auto r:rows){peers+=r.peers;if(r.state==State::Downloading)++active;}peakPeers=std::max(peakPeers,peers);peakActive=std::max(peakActive,active);return std::all_of(rows.begin(),rows.end(),[](auto r){return r.state==State::Complete;});},90);
            wait([&]{return complete(http.snapshot()).entries[0].status==downshift::QueueStatus::Completed;},40);measure=false;
            (void)complete(std::move(https));require(digest(QDir(root).filePath("https.bin")).toHex()==QByteArray(argv[2]),"Concurrent verified HTTPS SHA-256");require(digest(httpPath).toHex()==QByteArray(argv[2]),"Concurrent HTTP SHA-256");auto rows=complete(engine.snapshots());for(auto r:rows){require(r.verifiedBytes==r.selectedBytes,"Selected completion under load");for(auto f:r.files)if(f.priority)require(digest(QDir(r.directory).filePath(f.path))==digest(QDir(root).filePath("seed/"+f.path)),"Every selected payload SHA-256");}
            require(peakActive>=4&&peakPeers>=4&&peakPeers<=12,"Actual concurrent torrents obey session peer ceiling");require(beats>50&&gap<1.0,"Qt remains responsive during mixed transfers");
            PROCESS_MEMORY_COUNTERS_EX usage{};usage.cb=sizeof(usage);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&usage),sizeof(usage));DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);require(usage.PrivateUsage<baseline.PrivateUsage+384ull*1024*1024&&handles<handlesBefore+512,"Bounded private memory and handles for eight torrents");
            std::cout<<"T4 resource sample: peakActive="<<peakActive<<" peakPeers="<<peakPeers<<" privateGrowth="<<(usage.PrivateUsage>baseline.PrivateUsage?usage.PrivateUsage-baseline.PrivateUsage:0)<<" handleGrowth="<<(handles>handlesBefore?handles-handlesBefore:0)<<" maximumQtGap="<<gap<<std::endl;
            QJsonObject report{{"checks",checks},{"torrentCount",8},{"donorSessions",2},{"peakActive",peakActive},{"peakPeers",peakPeers},{"privateGrowthBytes",static_cast<double>(usage.PrivateUsage>baseline.PrivateUsage?usage.PrivateUsage-baseline.PrivateUsage:0)},{"handleGrowth",static_cast<int>(handles-handlesBefore)},{"maximumQtGapSeconds",gap},{"concurrentHttpSha256",true},{"verifiedHttpsSha256",true},{"allSelectedPayloadHashes",true}};QFile file(QString::fromLocal8Bit(argv[3]));require(file.open(QIODevice::WriteOnly),"Report opens");file.write(QJsonDocument(report).toJson());file.close();complete(engine.shutdown());
        }
        {TorrentEngine restored({false,true,db});wait([&]{const auto rows=complete(restored.snapshots());return std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.state==State::Complete;});});auto rows=complete(restored.snapshots());require(rows.size()==8,"All eight records survive restart");for(auto r:rows)require(r.nativePaused&&r.peers==0&&r.verifiedBytes==r.selectedBytes,"Restart verifies completion without downloading or seeding");complete(restored.shutdown());}
        std::cout<<"Torrent T4 mixed/resource checks passed: "<<checks<<std::endl;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
