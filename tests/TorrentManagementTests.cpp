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
#include <libtorrent/address.hpp>
#include <QCoreApplication>
#include <QProcess>
#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "torrent/TorrentStore.h"
#include <sqlite3.h>
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
lt::settings_pack localSettings(){lt::settings_pack s;s.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage);s.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);s.set_int(lt::settings_pack::min_reconnect_time,1);s.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");s.set_str(lt::settings_pack::dht_bootstrap_nodes,"");s.set_bool(lt::settings_pack::enable_dht,false);s.set_bool(lt::settings_pack::enable_lsd,false);s.set_bool(lt::settings_pack::enable_upnp,false);s.set_bool(lt::settings_pack::enable_natpmp,false);s.set_bool(lt::settings_pack::enable_outgoing_utp,false);s.set_bool(lt::settings_pack::enable_incoming_utp,false);return s;}

Snapshot row(TorrentEngine& engine,const QString& id){for(const auto& item:engine.snapshots().get())if(item.id==id)return item;throw std::runtime_error("Missing managed torrent ID");}
template<class F>void rejected(F operation,const char* message){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}require(failed,message);}
struct Seed {
    lt::session session{lt::session_params(localSettings())};
    lt::torrent_handle handle;
    Seed(const QString& root,const QString& metadata){auto params=lt::load_torrent_file(metadata.toUtf8().toStdString());params.save_path=QDir(root).filePath("seed").toUtf8().toStdString();params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode);lt::error_code error;handle=session.add_torrent(params,error);require(!error,"Seeder added");try{wait([&]{const auto state=handle.status();if(state.errc)throw std::runtime_error(state.errc.message());return state.is_seeding&&session.listen_port()!=0;});}catch(...){const auto state=handle.status();std::cerr<<"Seeder diagnostic: state="<<static_cast<int>(state.state)<<" error="<<state.errc.message()<<" wanted="<<state.total_wanted<<" verified="<<state.total_wanted_done<<" port="<<session.listen_port()<<std::endl;std::vector<lt::alert*> alerts;session.pop_alerts(&alerts);for(auto* alert:alerts)if(lt::alert_cast<lt::listen_failed_alert>(alert)||lt::alert_cast<lt::listen_succeeded_alert>(alert))std::cerr<<"Listen diagnostic: "<<alert->message()<<std::endl;throw;}}
    quint16 port(){return session.listen_port();}
};
void management(const QString& root,const QString& metadata,const QString& name){
    std::cout<<"Management/recovery: "<<name.toStdString()<<std::endl;
    Seed seed(root,metadata);const auto store=QDir(root).filePath(name+"-store.sqlite"),save=QDir(root).filePath(name+"-managed");QString id;
    {
        TorrentEngine engine({false,true,store});AddOptions options;options.paused=true;options.priorities={7,0};options.limits={128*1024,64*1024,3};
        id=engine.addFile(metadata,save,options).get();require(row(engine,id).state==State::Paused,"Add paused performs no transfer");require(row(engine,id).selectedBytes==512*1024+101,"Selected byte target excludes skipped file/padding");
        rejected([&]{engine.selectFiles(id,{0,0}).get();},"Empty selection rejected");rejected([&]{engine.selectFiles(id,{4}).get();},"Wrong priority count rejected");rejected([&]{engine.configure(id,{-1,0,1},{}).get();},"Invalid rate rejected");
        rejected([&]{engine.addFile(metadata,QDir::rootPath(),{true}).get();},"Filesystem root is not a torrent payload destination");
        rejected([&]{engine.remove(id,true,false).get();},"Delete requires explicit confirmation");require(row(engine,id).state==State::Paused,"Rejected deletion retains task");
        auto checkpoint=engine.checkpoint(id);auto pause=engine.pause(id);auto configuration=engine.configure(id,options.limits,{});checkpoint.get();pause.get();configuration.get();require(row(engine,id).state==State::Paused,"Overlapping durable commands settle without broken promises");
        engine.setSessionLimits({192*1024,96*1024,5}).get();
        {TorrentEngine competing({false,true,store});rejected([&]{competing.snapshots().get();},"Competing store owner rejected");competing.shutdown().get();}
        engine.resume(id).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();
        wait([&]{const auto r=row(engine,id);return r.verifiedBytes>=32768&&r.verifiedBytes<r.selectedBytes;});
        const auto partial=row(engine,id).verifiedBytes;require(row(engine,id).availability>=1,"Downloading snapshot calculates native distributed availability");engine.pause(id).get();
        require(row(engine,id).state==State::Paused,"Pause durable before return");std::this_thread::sleep_for(std::chrono::milliseconds(700));require(row(engine,id).state==State::Paused,"Paused state persists during native updates");
        require(row(engine,id).verifiedBytes>=partial&&row(engine,id).verifiedBytes<row(engine,id).selectedBytes,"Partial verified pieces preserved");engine.shutdown().get();
    }
    {
        TorrentEngine engine({false,true,store});auto r=row(engine,id);require(r.state==State::Paused&&r.id==id,"Restart restores same ID paused");require(r.files.size()==2&&r.files[0].priority==7&&r.files[1].priority==0,"Selected priorities survive restart");require(r.limits.download==128*1024&&r.limits.upload==64*1024&&r.limits.connections==3,"Per-task limits persist");require(engine.sessionLimits().get().download==192*1024&&engine.sessionLimits().get().connections==5,"Global torrent limits persist");
        const auto detail=engine.details(id).get();require(!detail.availability.empty(),"Copied native piece availability");
        engine.resume(id).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{return row(engine,id).state==State::Complete;},35);
        require(row(engine,id).verifiedBytes==row(engine,id).selectedBytes,"Selected completion uses wanted verified bytes");require(digest(QDir(save).filePath(name+"/file0.bin"))==digest(QDir(root).filePath("seed/"+name+"/file0.bin")),"Resumed selected file independent SHA-256");
        engine.selectFiles(id,{7,4}).get();engine.configure(id,{0,128*1024,4},{SeedMode::Manual,1,2}).get();engine.setSessionLimits({0,0,8}).get();engine.resume(id).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{return row(engine,id).state==State::Seeding;},30);
        require(row(engine,id).verifiedBytes==row(engine,id).totalBytes,"New selection downloads remaining file");require(digest(QDir(save).filePath(name+"/file1.bin"))==digest(QDir(root).filePath("seed/"+name+"/file1.bin")),"Newly selected file hash");
        engine.pause(id).get();engine.configure(id,{0,128*1024,4},{SeedMode::Time,1,2}).get();engine.resume(id).get();wait([&]{return row(engine,id).state==State::Complete;},10);require(row(engine,id).seedingSeconds>=2,"Time seeding policy stops and preserves elapsed time");
        engine.checkpoint(id).get();engine.shutdown().get();
    }
    // Same size and restored modification time must not make corrupt bytes trusted.
    const auto final=QDir(save).filePath(name+"/file0.bin");QFile corrupted(final);require(corrupted.open(QIODevice::ReadWrite),"Open completed file for corruption fixture");const auto modified=QFileInfo(final).lastModified();corrupted.write(QByteArray(16384,'x'));require(corrupted.setFileTime(modified,QFileDevice::FileModificationTime),"Corruption fixture preserves modification time");corrupted.close();
    {
        TorrentEngine engine({false,true,store});engine.configure(id,{0,128*1024,4},{}).get();engine.resume(id).get();
        wait([&]{const auto r=row(engine,id);return r.state==State::Downloading&&r.verifiedBytes<r.selectedBytes;},15);require(row(engine,id).state!=State::Complete,"Restored file rehash catches same-size corruption");
        engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{return row(engine,id).state==State::Complete;});require(digest(final)==digest(QDir(root).filePath("seed/"+name+"/file0.bin")),"Native peer repair restores corrupt piece");
        QFile unrelated(QDir(save).filePath("unrelated.txt"));require(unrelated.open(QIODevice::WriteOnly),"Unrelated file fixture");unrelated.write("retain");unrelated.close();engine.remove(id,true,true).get();require(engine.snapshots().get().empty(),"Confirmed delete removes torrent record after native disk acknowledgement");require(!QFileInfo::exists(final)&&QFileInfo::exists(unrelated.fileName()),"Deletion targets payload, retains unrelated files");engine.shutdown().get();
    }
    {TorrentEngine engine({false,true,store});require(engine.snapshots().get().empty(),"Explicit empty torrent store survives restart");engine.shutdown().get();}
}
void storage(const QString& root,const QString& metadata){
    const auto store=QDir(root).filePath("privacy.sqlite"),save=QDir(root).filePath("privacy-data");QString id;
    {TorrentEngine engine({false,true,store});id=engine.addFile(metadata,save,{true}).get();engine.shutdown().get();}
    QFile bytes(store);require(bytes.open(QIODevice::ReadOnly),"Encrypted database opens read-only");const auto raw=bytes.readAll();bytes.close();require(!raw.contains(save.toUtf8())&&!raw.contains("file0.bin")&&!raw.contains("seedMilliseconds"),"Torrent metadata/destinations/configuration absent from plaintext SQLite");
    QByteArray original;const auto backup=digest(store+".backup.sqlite");
    {TorrentStore direct(store);const auto entries=direct.load();require(entries.size()==1,"Authenticated store records available");original=entries[0].second;const auto object=QJsonDocument::fromJson(entries[0].second).object();require(object.value("version").toInt()==1,"Versioned encrypted payload");direct.save(id,"{\"version\":999}");}
    {TorrentEngine engine({false,true,store});rejected([&]{engine.snapshots().get();},"Unsupported encrypted record fails visibly");engine.shutdown().get();}
    require(digest(store+".backup.sqlite")==backup,"Invalid record does not rotate over the valid encrypted backup");
    for(int mode=0;mode<2;++mode){
        auto object=QJsonDocument::fromJson(original).object();if(mode)object["hashes"]=QJsonArray{"v1:"+QString(40,'0')};else object["native"]=QString::fromLatin1(QByteArray("invalid native resume").toBase64());
        {TorrentStore direct(store);direct.save(id,QJsonDocument(object).toJson(QJsonDocument::Compact));}
        {TorrentEngine engine({false,true,store});rejected([&]{engine.snapshots().get();},"Authenticated invalid native resume/hash rejected");engine.shutdown().get();}
    }
    sqlite3* db=nullptr;require(sqlite3_open(store.toUtf8().constData(),&db)==SQLITE_OK,"Private corruption database opens");require(sqlite3_exec(db,"UPDATE torrents SET payload=randomblob(96)",nullptr,nullptr,nullptr)==SQLITE_OK,"DPAPI tamper fixture");sqlite3_close(db);
    {TorrentEngine engine({false,true,store});rejected([&]{engine.snapshots().get();},"Tampered DPAPI record rejected");engine.shutdown().get();}
    const auto renamed=store+".retained";require(QFile::rename(store,renamed),"Missing-live fixture preserves corrupt file");
    {TorrentEngine engine({false,true,store});rejected([&]{engine.snapshots().get();},"Owned missing store fails instead of creating empty history");engine.shutdown().get();}require(!QFileInfo::exists(store),"Missing history not silently replaced");
    const auto http=QDir(root).filePath("queue.sqlite");{TorrentEngine engine({false,true,http});rejected([&]{engine.snapshots().get();},"HTTP queue path refused by torrent subsystem");engine.shutdown().get();}require(!QFileInfo::exists(http),"HTTP queue never created/migrated");
    const auto alien=QDir(root).filePath("renamed-http.sqlite");require(sqlite3_open(alien.toUtf8().constData(),&db)==SQLITE_OK,"Foreign store fixture");sqlite3_exec(db,"CREATE TABLE downloads(id TEXT PRIMARY KEY,payload BLOB)",nullptr,nullptr,nullptr);sqlite3_close(db);
    {TorrentEngine engine({false,true,alien});rejected([&]{engine.snapshots().get();},"Renamed HTTP database is also refused");engine.shutdown().get();}
}
void lifecycle(const QString& root,const QString& metadata,const QString& magnetMetadata){
    const auto store=QDir(root).filePath("lifecycle.sqlite");QString id,magnetId;
    {
        TorrentEngine engine({false,true,store});id=engine.addFile(metadata,QDir(root).filePath("keep"),{true}).get();
        const auto uri=QString::fromStdString(lt::make_magnet_uri(lt::load_torrent_file(magnetMetadata.toUtf8().toStdString())));
        magnetId=engine.addMagnet(uri,QDir(root).filePath("pending-metadata")).get();require(engine.snapshots().get().size()==2,"Multiple torrents coexist independently");
        engine.pause(magnetId).get();engine.cancel(id).get();require(row(engine,id).state==State::Cancelled,"Durable cancel retains history");engine.shutdown().get();
    }
    {
        TorrentEngine engine({false,true,store});require(row(engine,id).state==State::Cancelled,"Cancelled record survives restart");require(row(engine,magnetId).state==State::Paused&&!row(engine,magnetId).metadata,"Magnet without metadata survives restart paused");
        engine.remove(id,true,true).get();engine.remove(magnetId).get();require(engine.snapshots().get().empty(),"Remove-only handles cancelled and pending metadata records");engine.shutdown().get();
    }
}
void selectionStress(const QString& root,const QString& metadata){
    std::cout<<"Selection/write completion ordering"<<std::endl;
    Seed seed(root,metadata);const auto name=QString::fromStdString(lt::load_torrent_file(metadata.toUtf8().toStdString()).ti->name());
    for(int n=0;n<6;++n){
        TorrentEngine engine({false,true});AddOptions config;config.priorities={7,0};config.seeding.mode=SeedMode::Manual;
        const auto save=QDir(root).filePath(QString("selection-order-%1").arg(n));const auto id=engine.addFile(metadata,save,config).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();
        std::cout<<"Ordering iteration "<<n<<" first selection"<<std::endl;
        wait([&]{return row(engine,id).state==State::Seeding;});
        require(digest(QDir(save).filePath(name+"/file0.bin"))==digest(QDir(root).filePath("seed/"+name+"/file0.bin")),"Seeding starts after selected-file disk flush");
        std::cout<<"Ordering iteration "<<n<<" expanded selection"<<std::endl;engine.selectFiles(id,{7,4}).get();const auto changed=row(engine,id);require(changed.state!=State::Seeding||changed.verifiedBytes==changed.selectedBytes,"Changing selection invalidates an earlier seeding snapshot");
        wait([&]{return row(engine,id).state==State::Seeding;});
        const auto actualPath=QDir(save).filePath(name+"/file1.bin"),expectedPath=QDir(root).filePath("seed/"+name+"/file1.bin");
        if(digest(actualPath)!=digest(expectedPath)){
            QFile actual(actualPath),expected(expectedPath);(void)actual.open(QIODevice::ReadOnly);(void)expected.open(QIODevice::ReadOnly);const auto a=actual.readAll(),b=expected.readAll();qsizetype first=0;while(first<std::min(a.size(),b.size())&&a[first]==b[first])++first;
            std::cerr<<"Selection disk diagnostic: iteration="<<n<<" actualSize="<<a.size()<<" expectedSize="<<b.size()<<" firstDifference="<<first<<" actualByte="<<(first<a.size()?static_cast<unsigned char>(a[first]):-1)<<" expectedByte="<<(first<b.size()?static_cast<unsigned char>(b[first]):-1)<<std::endl;
            require(false,"Expanded seeding snapshot waits for migrated/new file bytes on disk");
        }
        ++checks;engine.shutdown().get();
    }
}

void resolvedMagnet(const QString& root,const QString& metadata){
    std::cout<<"Resolved magnet persistence"<<std::endl;
    Seed seed(root,metadata);const auto params=lt::load_torrent_file(metadata.toUtf8().toStdString());
    const auto uri="magnet:?xt=urn:btih:"+QString::fromLatin1(QByteArray(params.info_hashes.v1.data(),20).toHex());
    const auto store=QDir(root).filePath("resolved-magnet.sqlite"),save=QDir(root).filePath("resolved-magnet-data");QString id;
    {
        TorrentEngine engine({false,true,store});AddOptions options;options.priorities={7,0};options.limits={64*1024,128*1024,4};
        id=engine.addMagnet(uri,save,options).get();require(!row(engine,id).metadata,"V1-only magnet begins without hybrid metadata");engine.connectPeer(id,"127.0.0.1",seed.port()).get();
        wait([&]{const auto value=row(engine,id);return value.metadata&&value.verifiedBytes>=32768&&value.verifiedBytes<value.selectedBytes;},30);
        engine.pause(id).get();engine.shutdown().get();
    }
    {
        TorrentEngine engine({false,true,store});const auto value=row(engine,id);require(value.metadata&&value.files.size()==2,"Resolved hybrid metadata persists without another peer fetch");require(value.state==State::Paused&&value.files[1].priority==0,"Resolved magnet selection restores paused");
        engine.configure(id,{0,128*1024,4},{}).get();engine.resume(id).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{return row(engine,id).state==State::Complete;});
        const auto name=QString::fromStdString(params.ti->name());require(digest(QDir(save).filePath(name+"/file0.bin"))==digest(QDir(root).filePath("seed/"+name+"/file0.bin")),"Resolved magnet restart final selected-file SHA-256");engine.shutdown().get();
    }
}

void unexplainedEmpty(const QString& root,const QString& metadata){
    const auto store=QDir(root).filePath("unexplained-empty.sqlite");
    {TorrentEngine engine({false,true,store});engine.addFile(metadata,QDir(root).filePath("empty-protected-data"),{true}).get();engine.shutdown().get();}
    const auto backup=digest(store+".backup.sqlite");sqlite3* db=nullptr;require(sqlite3_open(store.toUtf8().constData(),&db)==SQLITE_OK,"Private empty-history fixture opens");
    require(sqlite3_exec(db,"DELETE FROM torrents",nullptr,nullptr,nullptr)==SQLITE_OK,"Structurally valid unexplained empty-history fixture");sqlite3_close(db);
    {TorrentEngine engine({false,true,store});rejected([&]{engine.snapshots().get();},"Unexpected empty torrent history fails visibly");engine.shutdown().get();}
    require(digest(store+".backup.sqlite")==backup,"Unexpected empty history does not replace its nonempty encrypted backup");
}

void physicalIdentity(const QString& root,const QString& metadata){
    const auto path=QDir(root).filePath("physical.sqlite"),alias=QDir(root).filePath("physical-alias.sqlite"),old=path+".old";
    {
        TorrentEngine engine({false,true,path});engine.addFile(metadata,QDir(root).filePath("physical-data"),{true}).get();
        const auto nativePath=QDir::toNativeSeparators(path).toStdWString(),nativeAlias=QDir::toNativeSeparators(alias).toStdWString();
        require(CreateHardLinkW(nativeAlias.c_str(),nativePath.c_str(),nullptr)!=0,"Private hard-link store alias");
        {TorrentEngine competing({false,true,alias});rejected([&]{competing.snapshots().get();},"Physical store lease rejects alternate hard-link path");competing.shutdown().get();}
        engine.shutdown().get();
    }
    require(QFile::remove(alias),"Private alias removed");require(QFile::rename(path,old)&&QFile::copy(old,path),"Same-content different physical store fixture");
    const auto before=digest(path);
    {TorrentEngine engine({false,true,path});rejected([&]{engine.snapshots().get();},"Store identity rejects physical replacement behind the same path");engine.shutdown().get();}
    require(digest(path)==before,"Rejected physical store remains unchanged");
}

void capsAndRatio(const QString& root,const QString& metadata){
    Seed seed(root,metadata);const auto params=lt::load_torrent_file(metadata.toUtf8().toStdString());const auto name=QString::fromStdString(params.ti->name());
    for(int mode=0;mode<2;++mode){
        std::cout<<"Cap transfer mode "<<mode<<std::endl;TorrentEngine engine({false,true});AddOptions config;config.priorities={4,0};config.limits={mode?64*1024:0,128*1024,2};
        if(!mode)engine.setSessionLimits({64*1024,128*1024,3}).get();
        const auto id=engine.addFile(metadata,QDir(root).filePath(QString("caps-%1").arg(mode)),config).get();
        const auto begin=std::chrono::steady_clock::now();engine.connectPeer(id,"127.0.0.1",seed.port()).get();
        wait([&]{return row(engine,id).state==State::Complete;},35);
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();require(seconds>=6,"Loopback transfer honors global/per-task 64 KiB/s cap");
        std::cout<<"Download cap "<<mode<<": "<<seconds<<" seconds"<<std::endl;
        const auto before=row(engine,id);require(before.downloadedBytes>0,"Native persistent download counter");
        require(digest(QDir(root).filePath(QString("caps-%1/").arg(mode)+name+"/file0.bin"))==digest(QDir(root).filePath("seed/"+name+"/file0.bin")),"Capped complete transfer SHA-256");
        {
            if(!mode)engine.setSessionLimits({0,64*1024,3}).get();engine.configure(id,{0,mode?64*1024:128*1024,3},{SeedMode::Ratio,0.8,3600}).get();engine.resume(id).get();wait([&]{return row(engine,id).state==State::Seeding;});
            lt::session leecher{lt::session_params(localSettings())};auto wanted=params;wanted.save_path=QDir(root).filePath(QString("ratio-leecher-%1").arg(mode)).toUtf8().toStdString();wanted.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode);wanted.file_priorities.assign(static_cast<std::size_t>(wanted.ti->layout().num_files()),lt::dont_download);wanted.file_priorities[0]=lt::default_priority;lt::error_code error;auto handle=leecher.add_torrent(wanted,error);require(!error,"Ratio test native leecher");wait([&]{return leecher.listen_port()!=0;});wait([&]{return handle.status().state==lt::torrent_status::downloading;});handle.connect_peer(lt::tcp::endpoint(lt::make_address("127.0.0.1"),engine.listeningPort().get()));
            wait([&]{return row(engine,id).uploadedBytes>=65536;},25);
            const auto first=row(engine,id).uploadedBytes;const auto start=std::chrono::steady_clock::now();
            wait([&]{return row(engine,id).state==State::Complete;},35);
            const auto value=row(engine,id);const auto span=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            require(value.uploadedBytes>first&&value.ratio>=0.8,"Ratio policy stops after actual upload target");
            require(span+0.6>=static_cast<double>(value.uploadedBytes-first)/(64*1024*1.75),"Actual payload growth honors global/per-task upload cap with native burst tolerance");
            std::cout<<"Upload cap "<<mode<<": "<<(value.uploadedBytes-first)<<" bytes in "<<span<<" seconds, final ratio "<<value.ratio<<std::endl;
        }
        engine.shutdown().get();
    }
}
void crash(const QString& root,const QString& metadata){
    Seed seed(root,metadata);const auto store=QDir(root).filePath("crash.sqlite"),save=QDir(root).filePath("crash-data"),marker=QDir(root).filePath("crash-ready");
    QProcess child;child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});child.setProgram(QCoreApplication::applicationFilePath());child.setArguments({"--crash-child",store,metadata,save,QString::number(seed.port()),marker});child.start();require(child.waitForStarted(5000),"Hidden crash fixture child starts");
    wait([&]{if(child.state()==QProcess::NotRunning)throw std::runtime_error(child.readAllStandardError().toStdString());return QFileInfo::exists(marker);},30);
    QFile ready(marker);require(ready.open(QIODevice::ReadOnly),"Durable checkpoint marker opens");const auto id=QString::fromUtf8(ready.readAll());ready.close();child.kill();require(child.waitForFinished(5000),"Forced process death after durable checkpoint");
    TorrentEngine engine({false,true,store});require(row(engine,id).state==State::Paused,"Crash restores paused without auto-transfer");engine.resume(id).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{return row(engine,id).state==State::Complete;});
    const auto name=QString::fromStdString(lt::load_torrent_file(metadata.toUtf8().toStdString()).ti->name());const auto final=QDir(save).filePath(name+"/file0.bin");require(digest(final)==digest(QDir(root).filePath("seed/"+name+"/file0.bin")),"Process-death recovery final SHA-256");
    engine.remove(id).get();require(QFileInfo::exists(final)&&engine.snapshots().get().empty(),"Remove-only retains downloaded file");engine.shutdown().get();
}
int crashChild(const QStringList& args){
    TorrentEngine engine({false,true,args[2]});AddOptions config;config.priorities={4,0};config.limits={64*1024,128*1024,3};const auto id=engine.addFile(args[3],args[4],config).get();engine.connectPeer(id,"127.0.0.1",static_cast<quint16>(args[5].toUInt())).get();
    wait([&]{const auto value=row(engine,id);return value.verifiedBytes>=32768&&value.verifiedBytes<value.selectedBytes;});engine.checkpoint(id).get();QSaveFile marker(args[6]);if(!marker.open(QIODevice::WriteOnly)||marker.write(id.toUtf8())!=id.toUtf8().size()||!marker.commit())throw std::runtime_error("Crash marker failed");
    for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==7&&app.arguments()[1]=="--crash-child"){try{return crashChild(app.arguments());}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
    QTemporaryDir temp;
    try{
        require(temp.isValid(),"Private management fixtures");
        const auto v1=create(temp.path(),lt::create_torrent::v1_only,"manage-v1"),v2=create(temp.path(),lt::create_torrent::v2_only,"manage-v2"),hybrid=create(temp.path(),{},"manage-hybrid");
        if(app.arguments().contains("--selection-race")){selectionStress(temp.path(),v1);std::cout<<"Selection ordering checks: "<<checks<<std::endl;return 0;}
        if(app.arguments().contains("--caps")){capsAndRatio(temp.path(),v1);std::cout<<"Caps/ratio checks: "<<checks<<std::endl;return 0;}
        management(temp.path(),v1,"manage-v1");management(temp.path(),v2,"manage-v2");management(temp.path(),hybrid,"manage-hybrid");std::cout<<"Storage validation"<<std::endl;storage(temp.path(),v1);std::cout<<"Lifecycle validation"<<std::endl;lifecycle(temp.path(),v1,v2);capsAndRatio(temp.path(),v1);std::cout<<"Process-death validation"<<std::endl;crash(temp.path(),v1);physicalIdentity(temp.path(),v1);resolvedMagnet(temp.path(),hybrid);selectionStress(temp.path(),v1);unexplainedEmpty(temp.path(),v1);
        std::cout<<"Torrent management checks passed: "<<checks<<" (native selected transfers, pause/restart/rehash, policies, encryption, ownership and deletion)\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
