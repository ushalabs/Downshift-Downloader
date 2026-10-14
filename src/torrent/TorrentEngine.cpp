#include "TorrentEngine.h"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace downshift::torrent {
namespace lt=libtorrent;
namespace {
constexpr int maxMetadata=10'000'000, maxFiles=10000, maxTorrents=8;
[[noreturn]] void reject(const char* message){throw std::runtime_error(message);}
QString text(const std::string& value){return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size()));}
std::vector<QString> hashes(const lt::info_hash_t& info){
    std::vector<QString> result;
    if(info.has_v1())result.push_back("v1:"+QString::fromLatin1(QByteArray(info.v1.data(),20).toHex()));
    if(info.has_v2())result.push_back("v2:"+QString::fromLatin1(QByteArray(info.v2.data(),32).toHex()));
    return result;
}
bool overlaps(const lt::info_hash_t& a,const lt::info_hash_t& b){
    return (a.has_v1()&&b.has_v1()&&a.v1==b.v1)||(a.has_v2()&&b.has_v2()&&a.v2==b.v2);
}
void noLinks(QString path){
    // Reject existing junctions/symlinks in both save roots and metadata paths.
    for(;;){const QFileInfo info(path);if(info.isSymLink()||info.isJunction())reject("Torrent destination contains a filesystem link.");const auto parent=info.absolutePath();if(parent==path)break;path=parent;}
}
QString destination(const QString& input){
    if(input.isEmpty()||input.contains(QChar(0))||!QDir::isAbsolutePath(input))reject("Choose an absolute torrent destination directory.");
    const auto path=QDir::cleanPath(input);noLinks(path);
    if(!QDir().mkpath(path))reject("The torrent destination is not writable.");
    return path;
}
std::vector<File> files(const lt::torrent_info& info,const QString& directory){
    const auto& layout=info.layout();if(layout.num_files()<=0||layout.num_files()>maxFiles)reject("Torrent exceeds the file-count limit.");
    std::vector<File> result;std::set<QString> names;
    const QRegularExpression device("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$",QRegularExpression::CaseInsensitiveOption);
    for(const auto index:layout.file_range()){
        if(layout.file_flags(index)&lt::file_storage::flag_symlink)reject("Torrent symlinks are not supported.");
        if(layout.file_flags(index)&lt::file_storage::flag_pad_file)continue;
        const auto path=text(layout.file_path(index)).replace('\\','/');
        if(QDir::isAbsolutePath(path)||path.isEmpty()||path.contains(QChar(0)))reject("Torrent contains an unsafe filename.");
        for(const auto& part:path.split('/'))if(part.isEmpty()||part=="."||part==".."||part.endsWith('.')||part.endsWith(' ')||part.contains(QRegularExpression("[<>:\"|?*\\x00-\\x1f]"))||device.match(part).hasMatch())reject("Torrent contains an unsafe Windows filename.");
        if(!names.insert(path.toCaseFolded()).second)reject("Torrent contains colliding Windows filenames.");
        noLinks(QDir(directory).filePath(path));result.push_back({path,layout.file_size(index)});
    }
    if(result.empty())reject("Torrent has no downloadable files.");return result;
}
}

struct TorrentEngine::Impl {
    struct Row {Snapshot view;lt::info_hash_t info;lt::torrent_handle handle;std::shared_ptr<std::promise<QString>> adding;};
    TorrentEngine& owner;Options options;
    std::mutex mutex;std::condition_variable condition;std::deque<std::function<void()>> commands;
    std::atomic<bool> wake=false;bool closing=false;QString fatal;
    std::promise<void> stopped;std::shared_future<void> finished=stopped.get_future().share();
    std::unique_ptr<lt::session> session;std::map<QString,Row> rows;
    std::jthread worker; // joins before session, callbacks, condition and owner disappear
    Impl(TorrentEngine& parent,Options config):owner(parent),options(config),worker([this]{run();}){}
    ~Impl(){stop();worker.join();}
    void stop(){std::lock_guard lock(mutex);closing=true;condition.notify_one();}
    bool enqueue(std::function<void()> command){std::lock_guard lock(mutex);if(closing)return false;commands.push_back(std::move(command));condition.notify_one();return true;}
    void healthy(){if(!fatal.isEmpty())throw std::runtime_error(fatal.toStdString());}
    template<class F> auto submit(F function){
        using Result=std::invoke_result_t<F>;
        auto task=std::make_shared<std::packaged_task<Result()>>([this,function=std::move(function)]()mutable{healthy();return function();});auto future=task->get_future();
        if(!enqueue([task]{(*task)();}))task.reset(); // broken promise is a defined shutdown failure
        return future;
    }
    std::future<QString> add(QString source,QString directory,bool magnet){
        auto promise=std::make_shared<std::promise<QString>>();auto future=promise->get_future();
        if(!enqueue([this,source=std::move(source),directory=std::move(directory),magnet,promise]{
            QString id;
            try{
                healthy();if(rows.size()>=maxTorrents)reject("Torrent session limit reached.");
                lt::add_torrent_params params;lt::error_code error;
                if(magnet){if(source.size()>32768||source.contains(QChar(0)))reject("Magnet URI exceeds the input limit.");params=lt::parse_magnet_uri(source.toUtf8().toStdString(),error);if(error)reject("Invalid magnet URI.");}
                else{QFile file(source);if(!file.open(QIODevice::ReadOnly)||file.size()>maxMetadata)reject("Torrent metadata is unavailable or exceeds the input limit.");const auto bytes=file.read(maxMetadata+1);lt::load_torrent_limits limits;params=lt::load_torrent_buffer(lt::span<char const>(bytes.constData(),static_cast<std::size_t>(bytes.size())),error,limits);if(error)reject("Invalid torrent metadata.");}
                if(params.ti)params.info_hashes=params.ti->info_hashes();if(hashes(params.info_hashes).empty())reject("Torrent has no supported info hash.");
                for(const auto& [key,row]:rows)if(row.view.state!=State::Cancelled&&overlaps(row.info,params.info_hashes))reject("This torrent is already in the session.");
                Row row;row.view.directory=destination(directory);row.info=params.info_hashes;row.adding=promise;
                if(params.ti){row.view.files=files(*params.ti,row.view.directory);row.view.name=text(params.ti->name());row.view.metadata=true;row.view.totalBytes=params.ti->size_on_disk();}
                // A magnet may describe file selection, web seeds or local paths. T1
                // always verifies/downloads all files from peers and ignores those extras.
                params.file_priorities.clear();params.renamed_files.clear();params.url_seeds.clear();
                params.save_path=QDir::toNativeSeparators(row.view.directory).toUtf8().toStdString();
                params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode|lt::torrent_flags::share_mode|lt::torrent_flags::default_dont_download|lt::torrent_flags::no_verify_files);
                params.flags|=lt::torrent_flags::duplicate_is_error;
                if(magnet)params.flags|=lt::torrent_flags::upload_mode; // validate metadata before payload writes
                params.max_connections=64;params.upload_limit=256*1024;
                id=QUuid::createUuid().toString(QUuid::Id128);row.view.id=id;rows.emplace(id,std::move(row));session->async_add_torrent(std::move(params));
            }catch(...){if(!id.isEmpty())rows.erase(id);promise->set_exception(std::current_exception());}
        }))promise->set_exception(std::make_exception_ptr(std::runtime_error("Torrent engine is shutting down.")));
        return future;
    }
    Row* find(const lt::torrent_handle& handle){for(auto& [id,row]:rows)if(row.handle==handle)return &row;return nullptr;}
    void fail(Row& row,const char* message){row.view.state=State::Failed;row.view.error=QString::fromUtf8(message);try{if(session&&row.handle.is_valid())session->remove_torrent(row.handle);}catch(...){}if(row.adding){row.adding->set_exception(std::make_exception_ptr(std::runtime_error(message)));row.adding.reset();}emit owner.updated(row.view);}
    void metadata(Row& row){
        const auto info=row.handle.torrent_file();if(!info)return;
        try{row.view.files=files(*info,row.view.directory);row.info=info->info_hashes();row.view.name=text(info->name());row.view.totalBytes=info->size_on_disk();row.view.metadata=true;row.handle.unset_flags(lt::torrent_flags::upload_mode);emit owner.updated(row.view);}
        catch(const std::exception&){fail(row,"Received torrent metadata has unsupported or unsafe paths.");}
    }
    void update(const lt::torrent_status& status){
        auto* row=find(status.handle);if(!row||row->view.state==State::Cancelled||row->view.state==State::Failed)return;
        auto& view=row->view;view.verifiedBytes=status.total_wanted_done;view.downloadBytesPerSecond=status.download_payload_rate;view.uploadBytesPerSecond=status.upload_payload_rate;view.peers=status.num_peers;
        if(status.errc){fail(*row,"Torrent transfer failed. Check disk space, permissions and destination availability.");return;}
        if(!status.has_metadata)view.state=State::Metadata;
        else if(status.is_seeding){view.state=State::Complete;view.verifiedBytes=view.totalBytes;view.downloadBytesPerSecond=view.uploadBytesPerSecond=0;row->handle.pause();}
        else if(status.state==lt::torrent_status::checking_files||status.state==lt::torrent_status::checking_resume_data)view.state=State::Checking;
        else view.state=State::Downloading;
        emit owner.updated(view);
    }
    void alerts(){
        std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);
        for(auto* alert:alerts){
            if(auto* added=lt::alert_cast<lt::add_torrent_alert>(alert)){
                for(auto& [id,row]:rows)if(row.adding&&overlaps(row.info,added->params.info_hashes)){
                    if(added->error){row.view.state=State::Failed;row.view.error="Torrent could not be added.";row.adding->set_exception(std::make_exception_ptr(std::runtime_error("Torrent could not be added (duplicate or resource failure).")));}
                    else{row.handle=added->handle;row.view.state=row.view.metadata?State::Checking:State::Metadata;row.adding->set_value(id);}
                    row.adding.reset();emit owner.updated(row.view);break;
                }
            }else if(auto* received=lt::alert_cast<lt::metadata_received_alert>(alert)){if(auto* row=find(received->handle))metadata(*row);}
            else if(auto* state=lt::alert_cast<lt::state_update_alert>(alert)){for(const auto& item:state->status)update(item);}
            else if(auto* error=lt::alert_cast<lt::torrent_error_alert>(alert)){if(auto* row=find(error->handle))fail(*row,"Torrent transfer failed. Check destination access and available space.");}
            else if(lt::alert_cast<lt::alerts_dropped_alert>(alert))reject("Torrent alert queue overflow; session stopped safely.");
        }
    }
    void run()noexcept{
        try{
            lt::settings_pack settings;
            settings.set_str(lt::settings_pack::listen_interfaces,options.loopbackOnly?"127.0.0.1:0":"0.0.0.0:0,[::]:0");
            settings.set_bool(lt::settings_pack::enable_dht,options.discovery&&!options.loopbackOnly);
            settings.set_bool(lt::settings_pack::enable_lsd,options.discovery&&!options.loopbackOnly);
            settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);
            if(options.loopbackOnly)settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
            settings.set_int(lt::settings_pack::connections_limit,64);settings.set_int(lt::settings_pack::active_limit,maxTorrents);
            settings.set_int(lt::settings_pack::max_metadata_size,maxMetadata);
            settings.set_int(lt::settings_pack::alert_queue_size,2048);
            settings.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::status|lt::alert_category::storage);
            settings.set_int(lt::settings_pack::stop_tracker_timeout,1);
            session=std::make_unique<lt::session>(lt::session_params(settings));
            session->set_alert_notify([this]{wake=true;condition.notify_one();});
        }catch(...){fatal="Torrent session startup failed.";emit owner.engineFailed(fatal);}
        auto next=std::chrono::steady_clock::now();
        for(;;){
            std::deque<std::function<void()>> tasks;bool end;
            {std::unique_lock lock(mutex);condition.wait_until(lock,next,[this]{return closing||!commands.empty()||wake.exchange(false);});tasks.swap(commands);end=closing;}
            for(auto& task:tasks)task();if(end)break;
            if(fatal.isEmpty())try{alerts();if(std::chrono::steady_clock::now()>=next){session->post_torrent_updates();next=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);}}
            catch(...){fatal="Torrent session processing failed; HTTP downloads are unaffected.";emit owner.engineFailed(fatal);for(auto& [id,row]:rows)fail(row,"Torrent session stopped after an internal error.");}
            if(!fatal.isEmpty())next=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        }
        for(auto& [id,row]:rows)if(row.adding){row.adding->set_exception(std::make_exception_ptr(std::runtime_error("Torrent add interrupted by shutdown.")));row.adding.reset();}
        // Callback is disabled while its synchronization objects still exist.
        // The proxy joins libtorrent's network/disk workers on this worker, not Qt.
        try{if(session){session->set_alert_notify({});auto proxy=session->abort();session.reset();}}catch(...){session.reset();}
        stopped.set_value();
    }
};
TorrentEngine::TorrentEngine(Options options,QObject* parent):QObject(parent){qRegisterMetaType<Snapshot>();impl_=std::make_unique<Impl>(*this,options);}
TorrentEngine::~TorrentEngine()=default;
std::future<QString> TorrentEngine::addFile(QString file,QString directory){return impl_->add(std::move(file),std::move(directory),false);}
std::future<QString> TorrentEngine::addMagnet(QString uri,QString directory){return impl_->add(std::move(uri),std::move(directory),true);}
std::future<std::vector<Snapshot>> TorrentEngine::snapshots(){return impl_->submit([this]{std::vector<Snapshot> result;for(const auto& [id,row]:impl_->rows)result.push_back(row.view);return result;});}
std::future<void> TorrentEngine::cancel(QString id){return impl_->submit([this,id=std::move(id)]{auto found=impl_->rows.find(id);if(found==impl_->rows.end())reject("Unknown torrent.");auto& row=found->second;if(row.handle.is_valid())impl_->session->remove_torrent(row.handle);row.view.state=State::Cancelled;row.view.downloadBytesPerSecond=row.view.uploadBytesPerSecond=0;emit updated(row.view);});}
std::future<void> TorrentEngine::connectPeer(QString id,QString address,quint16 port){return impl_->submit([this,id=std::move(id),address=std::move(address),port]{if(!port)reject("Invalid peer port.");lt::error_code error;const auto ip=lt::make_address(address.toStdString(),error);if(error)reject("Invalid peer address.");const auto found=impl_->rows.find(id);if(found==impl_->rows.end()||!found->second.handle.is_valid())reject("Torrent is not ready.");found->second.handle.connect_peer(lt::tcp::endpoint(ip,port));});}
std::shared_future<void> TorrentEngine::shutdown(){impl_->stop();return impl_->finished;}
}
