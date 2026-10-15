#include "torrent/TorrentEngine.h"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/alert_types.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <chrono>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <algorithm>
namespace lt=libtorrent;
using namespace downshift::torrent;
namespace {
int checks=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
template<class F>void wait(F condition,int seconds=25){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(std::chrono::steady_clock::now()<end){QCoreApplication::processEvents();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error("Local torrent condition timed out");}
QByteArray digest(const QString& path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Final file opens");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&file),"Independent final hash");return hash.result();}
QString create(const QString& root,lt::create_flags_t flags,const QString& name){
    const auto source=QDir(root).filePath("seed/"+name);require(QDir().mkpath(source),"Fixture directory");
    for(int n=0;n<2;++n){QFile file(QDir(source).filePath(QString("file%1.bin").arg(n)));require(file.open(QIODevice::WriteOnly),"Fixture file opens");QByteArray bytes(512*1024+101+n*17,0);for(qsizetype i=0;i<bytes.size();++i)bytes[i]=static_cast<char>((i*31+n*17)%251);require(file.write(bytes)==bytes.size(),"Fixture bytes");}
    lt::create_torrent torrent(lt::list_files(QDir::toNativeSeparators(source).toUtf8().toStdString()),16384,flags);lt::error_code error;lt::set_piece_hashes(torrent,QDir::toNativeSeparators(QDir(root).filePath("seed")).toUtf8().toStdString(),error);require(!error,"Fixture native hashes");
    std::vector<char> buffer;lt::bencode(std::back_inserter(buffer),torrent.generate());const auto metadata=QDir(root).filePath(name+".torrent");QFile file(metadata);require(file.open(QIODevice::WriteOnly),"Metadata file opens");require(file.write(buffer.data(),static_cast<qint64>(buffer.size()))==static_cast<qint64>(buffer.size()),"Metadata writes");return metadata;
}
QString metadataOnly(const QString& root,const QString& name,std::vector<lt::create_file_entry> entries,lt::create_flags_t flags){
    lt::create_torrent torrent(std::move(entries),16384,flags);lt::sha1_hash hash;std::fill(hash.data(),hash.data()+20,'x');for(auto piece:torrent.piece_range())torrent.set_hash(piece,hash);
    const auto bytes=torrent.generate_buf();const auto path=QDir(root).filePath(name+".torrent");QFile file(path);require(file.open(QIODevice::WriteOnly),"Validation metadata opens");require(file.write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size()),"Validation metadata writes");return path;
}
lt::settings_pack localSettings(){lt::settings_pack s;s.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage);s.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:6881");s.set_str(lt::settings_pack::dht_bootstrap_nodes,"");s.set_bool(lt::settings_pack::enable_dht,false);s.set_bool(lt::settings_pack::enable_lsd,false);s.set_bool(lt::settings_pack::enable_upnp,false);s.set_bool(lt::settings_pack::enable_natpmp,false);s.set_bool(lt::settings_pack::enable_outgoing_utp,false);s.set_bool(lt::settings_pack::enable_incoming_utp,false);return s;}
void transfer(const QString& root,const QString& metadata,bool magnet){
    auto params=lt::load_torrent_file(QDir::toNativeSeparators(metadata).toUtf8().toStdString());const auto name=QString::fromUtf8(params.ti->name().c_str());
    std::cout<<"Local transfer: "<<name.toStdString()<<(magnet?" magnet":" metadata file")<<std::endl;
    lt::session seed{lt::session_params(localSettings())};params.save_path=QDir::toNativeSeparators(QDir(root).filePath("seed")).toUtf8().toStdString();params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode);lt::error_code error;auto handle=seed.add_torrent(params,error);require(!error,"Local seeder added");
    try{wait([&]{const auto state=handle.status();if(state.errc)throw std::runtime_error(state.errc.message());return state.is_seeding&&seed.listen_port()!=0;});}catch(...){const auto state=handle.status();std::cerr<<"Seeder diagnostic: state="<<static_cast<int>(state.state)<<" error="<<state.errc.message()<<" wanted="<<state.total_wanted<<" verified="<<state.total_wanted_done<<" port="<<seed.listen_port()<<std::endl;std::vector<lt::alert*> alerts;seed.pop_alerts(&alerts);for(auto* alert:alerts)if(lt::alert_cast<lt::listen_failed_alert>(alert)||lt::alert_cast<lt::listen_succeeded_alert>(alert))std::cerr<<"Listen diagnostic: "<<alert->message()<<std::endl;throw;}
    const auto save=QDir(root).filePath(name+(magnet?"-magnet":"-file"));
    TorrentEngine engine({false,true});int events=0;bool correctThread=true;QObject receiver;
    QObject::connect(&engine,&TorrentEngine::updated,&receiver,[&](Snapshot){correctThread=correctThread&&(QThread::currentThread()==receiver.thread());++events;},Qt::QueuedConnection);
    const auto id=(magnet?engine.addMagnet(QString::fromStdString(lt::make_magnet_uri(params)),save):engine.addFile(metadata,save)).get();
    if(magnet)require(!engine.snapshots().get()[0].metadata,"Magnet starts without metadata");
    bool duplicate=false;try{engine.addFile(metadata,save).get();}catch(const std::exception&){duplicate=true;}require(duplicate,"Duplicate rejected without another transfer");
    engine.connectPeer(id,"127.0.0.1",static_cast<quint16>(seed.listen_port())).get();
    wait([&]{auto row=engine.snapshots().get().front();if(row.state==State::Failed)throw std::runtime_error("Local transfer failed");return row.state==State::Complete;},35);
    const auto row=engine.snapshots().get().front();require(row.metadata&&row.files.size()==2&&row.totalBytes==1024*1024+219&&row.verifiedBytes==row.totalBytes,"Metadata and verified complete byte counts exclude pad files");
    engine.shutdown().get();
    for(int n=0;n<2;++n){const auto relative=name+QString("/file%1.bin").arg(n);require(digest(QDir(save).filePath(relative))==digest(QDir(root).filePath("seed/"+relative)),"Independent complete SHA-256 matches source");}
    QCoreApplication::processEvents();require(events>0,"Asynchronous status delivered");require(correctThread,"Qt updates are queued to receiver thread");
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temp;
    try{
        require(temp.isValid(),"Private fixture directory");require(LIBTORRENT_VERSION_NUM>=20100&&LIBTORRENT_VERSION_NUM<30000,"Rasterbar 2.1 ABI verified");
        require(QString::fromLatin1(lt::version())==QString::fromLatin1(LIBTORRENT_VERSION),"Runtime/header dependency versions match");
        std::cout<<"Rasterbar runtime "<<lt::version()<<" ABI "<<TORRENT_ABI_VERSION<<'\n';
        const auto v1=create(temp.path(),lt::create_torrent::v1_only,"v1");const auto v2=create(temp.path(),lt::create_torrent::v2_only,"v2");const auto hybrid=create(temp.path(),{},"hybrid");
        transfer(temp.path(),v1,false);transfer(temp.path(),v2,false);transfer(temp.path(),hybrid,false);transfer(temp.path(),v1,true);transfer(temp.path(),v2,true);transfer(temp.path(),hybrid,true);
        {
            TorrentEngine engine({false,true});const auto save=QDir(temp.path()).filePath("invalid");
            for(const auto& input:{QString("not-a-magnet"),QString("magnet:?xt=urn:btih:garbage"),QString(32769,'x')}){bool invalid=false;try{engine.addMagnet(input,save).get();}catch(const std::exception&){invalid=true;}require(invalid,"Invalid magnet bounded/rejected");}
            QFile corrupt(QDir(temp.path()).filePath("corrupt.torrent"));require(corrupt.open(QIODevice::WriteOnly),"Corrupt fixture");corrupt.write("bad metadata");corrupt.close();bool invalid=false;try{engine.addFile(corrupt.fileName(),save).get();}catch(const std::exception&){invalid=true;}require(invalid,"Corrupt metadata rejected");
            QFile oversized(QDir(temp.path()).filePath("oversized.torrent"));require(oversized.open(QIODevice::WriteOnly)&&oversized.resize(10'000'001),"Oversized fixture");oversized.close();invalid=false;try{engine.addFile(oversized.fileName(),save).get();}catch(const std::exception&){invalid=true;}require(invalid,"Metadata size bounded before loading");
            invalid=false;try{engine.addFile(v1,"relative-path").get();}catch(const std::exception&){invalid=true;}require(invalid,"Relative destination rejected");
            invalid=false;try{engine.addFile(v1,corrupt.fileName()).get();}catch(const std::exception&){invalid=true;}require(invalid,"Unusable destination rejected without crashing session");
            const auto links=metadataOnly(temp.path(),"links",{{"links/data.bin",1},{"links/link",0,lt::file_storage::flag_symlink,0,"data.bin"}},lt::create_torrent::v1_only|lt::create_torrent::symlinks);
            invalid=false;try{engine.addFile(links,save).get();}catch(const std::exception&){invalid=true;}require(invalid,"Metadata symlinks rejected before transfer");
            std::vector<lt::create_file_entry> many;for(int n=0;n<10001;++n)many.emplace_back("many/file"+std::to_string(n),1);const auto tooMany=metadataOnly(temp.path(),"many",std::move(many),lt::create_torrent::v1_only);
            invalid=false;try{engine.addFile(tooMany,save).get();}catch(const std::exception&){invalid=true;}require(invalid,"Metadata file count bounded before transfer");
            const auto magnet=QString::fromStdString(lt::make_magnet_uri(lt::load_torrent_file(v1.toUtf8().toStdString())));const auto id=engine.addMagnet(magnet,save).get();std::this_thread::sleep_for(std::chrono::milliseconds(800));require(engine.snapshots().get().front().state==State::Metadata,"Unavailable metadata remains loading, not invalid");engine.cancel(id).get();require(engine.snapshots().get().front().state==State::Cancelled,"Metadata cancellation");
            const auto shutdown=engine.shutdown();shutdown.get();require(shutdown.wait_for(std::chrono::seconds(0))==std::future_status::ready,"Asynchronous shutdown completes");invalid=false;try{engine.snapshots().get();}catch(const std::exception&){invalid=true;}require(invalid,"Commands after shutdown fail safely");
        }
        for(int n=0;n<3;++n){TorrentEngine engine({false,true});auto pending=engine.addFile(v1,QDir(temp.path()).filePath("shutdown"));engine.shutdown().get();try{(void)pending.get();}catch(const std::exception&){}++checks;}
        std::cout<<"Torrent foundation checks passed: "<<checks<<" (six complete local peer transfers, v1/v2/hybrid metadata and independent SHA-256)\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
