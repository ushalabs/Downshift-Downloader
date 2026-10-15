#pragma once
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
namespace t4 {
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
lt::settings_pack localSettings(){lt::settings_pack s;s.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage);s.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);s.set_int(lt::settings_pack::min_reconnect_time,1);s.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:6881");s.set_str(lt::settings_pack::dht_bootstrap_nodes,"");s.set_bool(lt::settings_pack::enable_dht,false);s.set_bool(lt::settings_pack::enable_lsd,false);s.set_bool(lt::settings_pack::enable_upnp,false);s.set_bool(lt::settings_pack::enable_natpmp,false);s.set_bool(lt::settings_pack::enable_outgoing_utp,false);s.set_bool(lt::settings_pack::enable_incoming_utp,false);return s;}

Snapshot row(TorrentEngine& engine,const QString& id){for(const auto& item:engine.snapshots().get())if(item.id==id)return item;throw std::runtime_error("Missing managed torrent ID");}
template<class F>void rejected(F operation,const char* message){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}require(failed,message);}
struct Seed {
    lt::session session{lt::session_params(localSettings())};
    lt::torrent_handle handle;
    Seed(const QString& root,const QString& metadata){auto params=lt::load_torrent_file(metadata.toUtf8().toStdString());params.save_path=QDir(root).filePath("seed").toUtf8().toStdString();params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode);lt::error_code error;handle=session.add_torrent(params,error);require(!error,"Seeder added");try{wait([&]{const auto state=handle.status();if(state.errc)throw std::runtime_error(state.errc.message());return state.is_seeding&&session.listen_port()!=0;});}catch(...){const auto state=handle.status();std::cerr<<"Seeder diagnostic: state="<<static_cast<int>(state.state)<<" error="<<state.errc.message()<<" wanted="<<state.total_wanted<<" verified="<<state.total_wanted_done<<" port="<<session.listen_port()<<std::endl;std::vector<lt::alert*> alerts;session.pop_alerts(&alerts);for(auto* alert:alerts)if(lt::alert_cast<lt::listen_failed_alert>(alert)||lt::alert_cast<lt::listen_succeeded_alert>(alert))std::cerr<<"Listen diagnostic: "<<alert->message()<<std::endl;throw;}}
    quint16 port(){return session.listen_port();}
};

}
