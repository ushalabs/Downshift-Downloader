#include "TorrentEngine.h"
#include "TorrentStore.h"
#ifdef DOWNSHIFT_TORRENT_FAULT_TEST
#include "TorrentFaultDisk.h"
#endif
#include "core/DownloadQueue/OwnedMedia.h"
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/ip_filter.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
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
#include <algorithm>
#include <cmath>
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
    const auto path=QDir::cleanPath(input);if(QDir(path).isRoot())reject("Choose a separate torrent folder below the filesystem root.");noLinks(path);
    if(!QDir().mkpath(path))reject("The torrent destination is not writable.");
    const auto resolved=QDir(path).canonicalPath();if(resolved.isEmpty())reject("The torrent destination could not be resolved.");return resolved;
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
        noLinks(QDir(directory).filePath(path));result.push_back({path,layout.file_size(index),0,static_cast<int>(index),4});
    }
    if(result.empty())reject("Torrent has no downloadable files.");return result;
}
}

namespace {
void validate(Limits limits){
    if(limits.download<0||limits.upload<0||limits.connections<1||limits.connections>512)reject("Invalid torrent bandwidth or connection limits.");
}
void validate(SeedPolicy policy){
    if(policy.mode<SeedMode::Stop||policy.mode>SeedMode::Manual||!std::isfinite(policy.ratio)||policy.ratio<=0||policy.ratio>1000||policy.seconds<1||policy.seconds>31*86400)reject("Invalid torrent seeding policy.");
}
void validate(const std::vector<int>& priorities){
    if(priorities.size()>maxFiles)reject("Too many file priorities.");
    for(auto priority:priorities)if(priority<0||priority>7)reject("File priorities must be between zero and seven.");
    if(!priorities.empty()&&std::all_of(priorities.begin(),priorities.end(),[](int p){return p==0;}))reject("Select at least one torrent file.");
}
QJsonObject encode(Limits value){return {{"download",value.download},{"upload",value.upload},{"connections",value.connections}};}
Limits decodeLimits(const QJsonObject& object){
    Limits value{object.value("download").toInt(-1),object.value("upload").toInt(-1),object.value("connections").toInt(-1)};validate(value);return value;
}
QJsonObject encode(SeedPolicy value){return {{"mode",static_cast<int>(value.mode)},{"ratio",value.ratio},{"seconds",value.seconds}};}
SeedPolicy decodePolicy(const QJsonObject& object){
    SeedPolicy value{static_cast<SeedMode>(object.value("mode").toInt(-1)),object.value("ratio").toDouble(-1),object.value("seconds").toInt(-1)};validate(value);return value;
}
QByteArray nativeBytes(const lt::add_torrent_params& params){const auto bytes=lt::write_resume_data_buf(params);if(bytes.size()>32*1024*1024)reject("Torrent resume data exceeds the storage limit.");return QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size()));}
std::exception_ptr failure(const char* message){return std::make_exception_ptr(std::runtime_error(message));}
}

struct TorrentEngine::Impl {
    enum class Removal { None, Cancel, Keep, Delete, Failure };
    struct Row {
        Snapshot view;
        lt::info_hash_t info;
        lt::torrent_handle handle;
        std::shared_ptr<std::promise<QString>> adding;
        QByteArray native;
        QJsonObject fileProofs;
        std::vector<int> priorities;
        bool paused=false, restored=false, saving=false, flushing=false, saveAgain=false, completionPending=false, seedingPending=false, blocked=false;
        bool priorityPending=false, saveAfterPriorities=false, recheckAfterPriorities=false;
        Removal removal=Removal::None;
        std::shared_ptr<std::promise<void>> removing;
        std::vector<std::shared_ptr<std::promise<void>>> waiters, nextWaiters, priorityWaiters;
        std::chrono::steady_clock::time_point seedTick=std::chrono::steady_clock::now();
        qint64 seedMilliseconds=0;
    };
    TorrentEngine& owner;
    Options options;
    Limits global{0,0,64};
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::function<void()>> commands;
    std::atomic<bool> wake=false;
    bool closing=false, drainingFlag=false;
    QString fatal;
    std::exception_ptr shutdownError;
    std::promise<void> stopped;
    std::shared_future<void> finished=stopped.get_future().share();
    std::unique_ptr<TorrentStore> store;
    std::unique_ptr<lt::session> session;
    std::map<QString,Row> rows;
    std::jthread worker;

    Impl(TorrentEngine& parent,Options config):owner(parent),options(std::move(config)),worker([this]{run();}){}
    ~Impl(){stop();worker.join();}
    void stop(){std::lock_guard lock(mutex);closing=true;condition.notify_one();}
    bool enqueue(std::function<void()> command){std::lock_guard lock(mutex);if(closing)return false;commands.push_back(std::move(command));condition.notify_one();return true;}
    void healthy(){if(!fatal.isEmpty())throw std::runtime_error(fatal.toStdString());}
    template<class F> auto submit(F function){
        using Result=std::invoke_result_t<F>;
        auto task=std::make_shared<std::packaged_task<Result()>>([this,function=std::move(function)]()mutable{healthy();return function();});auto future=task->get_future();
        if(!enqueue([task]{(*task)();}))task.reset();
        return future;
    }
    Row& ready(const QString& id){
        auto found=rows.find(id);
        if(found==rows.end()||!found->second.handle.is_valid()||found->second.adding||found->second.removal!=Removal::None||found->second.view.state==State::Cancelled||found->second.blocked)reject("Torrent is not ready for this operation.");
        return found->second;
    }
    Row* find(const lt::torrent_handle& handle){for(auto& [id,row]:rows)if(row.handle==handle)return &row;return nullptr;}
    Row* find(const lt::info_hash_t& info){for(auto& [id,row]:rows)if(overlaps(row.info,info))return &row;return nullptr;}
    void unique(const lt::info_hash_t& info,const QString& directory){
        if(!options.databasePath.isEmpty()){
            const auto db=QDir::cleanPath(options.databasePath).toCaseFolded(),save=QDir::cleanPath(directory).toCaseFolded();
            if(db==save||db.startsWith(save.endsWith('/')?save:save+"/"))reject("Torrent history must be stored outside payload destinations.");
        }
        for(const auto& [id,row]:rows){
            if(overlaps(row.info,info))reject("This torrent is already in the session. Remove its retained record before adding it again.");
            const auto a=QDir::cleanPath(directory).toCaseFolded(),b=QDir::cleanPath(row.view.directory).toCaseFolded();
            if(a==b||a.startsWith(b+"/")||b.startsWith(a+"/"))reject("Choose a separate destination for each torrent.");
        }
    }
    void applyPriorities(Row& row,lt::add_torrent_params* params=nullptr){
        if(!row.view.metadata)return;
        if(row.priorities.empty())row.priorities.assign(row.view.files.size(),4);
        if(row.priorities.size()!=row.view.files.size())reject("File priorities do not match the torrent file list.");
        const auto info=params?params->ti:row.handle.torrent_file();if(!info)reject("Torrent metadata is unavailable.");
        std::vector<lt::download_priority_t> native(static_cast<std::size_t>(info->layout().num_files()),lt::dont_download);
        row.view.selectedBytes=0;
        for(std::size_t n=0;n<row.view.files.size();++n){auto& file=row.view.files[n];file.priority=row.priorities[n];native[static_cast<std::size_t>(file.index)]=lt::download_priority_t(static_cast<std::uint8_t>(file.priority));if(file.priority)row.view.selectedBytes+=file.bytes;}
        if(params)params->file_priorities=std::move(native);
        else{row.priorityPending=true;row.handle.prioritize_files(native);}
    }
    void reserveFiles(Row& row){
        if(!row.view.metadata||row.view.metadataOnly)return;
        for(const auto& file:row.view.files)if(file.priority&&!row.fileProofs.contains(file.path)){
            const auto target=QDir(row.view.directory).filePath(file.path);noLinks(target);
            if(QFileInfo::exists(target)){row.fileProofs[file.path]=QJsonObject{{"external",true}};continue;}
            if(!QDir().mkpath(QFileInfo(target).absolutePath()))reject("Cannot create the torrent payload folder.");noLinks(target);
            ownedMedia::Handle handle{CreateFileW(std::filesystem::path(target.toStdWString()).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};BY_HANDLE_FILE_INFORMATION info{};
            if(handle.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(handle.value,&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))reject("Cannot reserve the torrent payload file safely.");
            row.fileProofs[file.path]=QJsonObject{{"identity",ownedMedia::identity(info)},{"complete",file.bytes==0}};
        }
    }
    void prepare(Row& row,lt::add_torrent_params& params,bool restore){
        params.save_path=QDir::toNativeSeparators(row.view.directory).toUtf8().toStdString();
        params.renamed_files.clear();params.url_seeds.clear();params.file_priorities.clear();params.piece_priorities.clear();
        params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode|lt::torrent_flags::share_mode|lt::torrent_flags::default_dont_download|lt::torrent_flags::no_verify_files|lt::torrent_flags::upload_mode);
        params.flags|=lt::torrent_flags::duplicate_is_error;
        if(row.paused||restore)params.flags|=lt::torrent_flags::paused;
        if(!params.ti||row.view.metadataOnly)params.flags|=lt::torrent_flags::upload_mode;
        params.max_connections=row.view.limits.connections;params.upload_limit=row.view.limits.upload;params.download_limit=row.view.limits.download;
        if(params.ti){row.info=params.ti->info_hashes();row.view.files=files(*params.ti,row.view.directory);row.view.name=text(params.ti->name());row.view.metadata=true;row.view.totalBytes=params.ti->size_on_disk();applyPriorities(row,&params);}
        if(restore){
            // Saved counters/configuration cannot prove that same-size/same-mtime
            // files still contain verified pieces. Rehash before reuse or seeding.
            params.have_pieces.clear();params.verified_pieces.clear();params.unfinished_pieces.clear();
        }
    }
    void persist(Row& row){
        if(!store)return;
        QJsonArray priorities,info;for(auto p:row.priorities)priorities.append(p);for(const auto& hash:hashes(row.info))info.append(hash);
        const QJsonObject object{{"version",1},{"id",row.view.id},{"directory",row.view.directory},{"hashes",info},{"paused",row.paused},{"metadataOnly",row.view.metadataOnly},{"created",QString::number(row.view.created)},{"cancelled",row.view.state==State::Cancelled},{"failed",row.view.state==State::Failed},{"removing",static_cast<int>(row.removal)},
            {"priorities",priorities},{"limits",encode(row.view.limits)},{"seeding",encode(row.view.seeding)},{"seedMilliseconds",QString::number(row.seedMilliseconds)},{"files",row.fileProofs},{"native",QString::fromLatin1(row.native.toBase64())}};
        try{store->save(row.view.id,QJsonDocument(object).toJson(QJsonDocument::Compact));}
        catch(...){row.paused=true;row.view.state=State::Failed;row.view.error="Torrent persistence failed; previous recovery data was retained.";if(row.handle.is_valid())row.handle.pause();throw;}
    }
    void restore(){
        if(!store)return;
        const auto savedSettings=store->settings();if(!savedSettings.isEmpty()){
            const auto object=QJsonDocument::fromJson(savedSettings).object();if(object.value("version").toInt()!=1)reject("Unsupported torrent settings version.");global=decodeLimits(object.value("limits").toObject());
        }
        std::vector<std::pair<QString,lt::add_torrent_params>> pending;
        for(const auto& [id,bytes]:store->load()){
            const auto object=QJsonDocument::fromJson(bytes).object();
            if(object.value("version").toInt()!=1||object.value("id").toString()!=id||!QRegularExpression("^[a-f0-9]{32}$").match(id).hasMatch())reject("Invalid torrent history record.");
            Row row;row.view.id=id;row.view.limits=decodeLimits(object.value("limits").toObject());row.view.seeding=decodePolicy(object.value("seeding").toObject());
            row.fileProofs=object.value("files").toObject();row.view.metadataOnly=object.value("metadataOnly").toBool();row.view.created=object.value("created").toString().toLongLong();
            row.view.directory=destination(object.value("directory").toString());row.paused=true;row.restored=true;
            bool validTime=false;row.seedMilliseconds=object.value("seedMilliseconds").toString().toLongLong(&validTime);if(!validTime||row.seedMilliseconds<0||row.seedMilliseconds>100LL*366*86400*1000)reject("Invalid torrent seeding counters.");
            for(const auto& p:object.value("priorities").toArray())row.priorities.push_back(p.toInt(-1));validate(row.priorities);
            const auto encoded=object.value("native").toString().toLatin1();if(encoded.size()>44*1024*1024)reject("Torrent resume data exceeds the storage limit.");
            const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);if(!decoded)reject("Invalid torrent resume encoding.");row.native=decoded.decoded;
            lt::error_code error;auto params=lt::read_resume_data(lt::span<char const>(row.native.constData(),static_cast<std::size_t>(row.native.size())),error);if(error)reject("Invalid native torrent resume data.");row.info=params.info_hashes;
            const auto expected=object.value("hashes").toArray();
            if(hashes(row.info).empty()||expected.isEmpty())reject("Torrent resume info hash is missing.");
            for(const auto& hash:hashes(row.info))if(!expected.contains(hash))reject("Torrent resume info hash does not match its record.");
            unique(row.info,row.view.directory);prepare(row,params,true);
            row.view.state=object.value("cancelled").toBool()?State::Cancelled:object.value("failed").toBool()?State::Failed:State::Paused;
            if(object.value("removing").toInt()!=0){row.view.state=State::Cancelled;row.view.error="Removal was interrupted. Files were retained where available; confirm removal again.";}
            rows.emplace(id,std::move(row));if(rows.at(id).view.state!=State::Cancelled&&rows.at(id).view.state!=State::Failed)pending.emplace_back(id,std::move(params));
        }
        lt::settings_pack settings;settings.set_int(lt::settings_pack::download_rate_limit,global.download);settings.set_int(lt::settings_pack::upload_rate_limit,global.upload);settings.set_int(lt::settings_pack::connections_limit,global.connections);session->apply_settings(settings);
        for(auto& [id,params]:pending){lt::error_code error;rows.at(id).handle=session->add_torrent(std::move(params),error);if(error)reject("Saved torrent could not be restored.");}
    }
    std::future<QString> add(QString source,QString directory,bool magnet,AddOptions config){
        auto promise=std::make_shared<std::promise<QString>>();auto future=promise->get_future();
        if(!enqueue([this,source=std::move(source),directory=std::move(directory),magnet,config=std::move(config),promise]{
            QString id;
            try{
                healthy();if(rows.size()>=maxTorrents)reject("Torrent session limit reached.");validate(config.limits);validate(config.seeding);validate(config.priorities);
                lt::add_torrent_params params;lt::error_code error;
                if(magnet){if(source.size()>32768||source.contains(QChar(0)))reject("Magnet URI exceeds the input limit.");params=lt::parse_magnet_uri(source.toUtf8().toStdString(),error);if(error)reject("Invalid magnet URI.");}
                else{QFile file(source);if(!file.open(QIODevice::ReadOnly)||file.size()>maxMetadata)reject("Torrent metadata is unavailable or exceeds the input limit.");const auto bytes=file.read(maxMetadata+1);params=lt::load_torrent_buffer(lt::span<char const>(bytes.constData(),static_cast<std::size_t>(bytes.size())),error,lt::load_torrent_limits{});if(error)reject("Invalid torrent metadata.");}
                if(params.ti)params.info_hashes=params.ti->info_hashes();if(hashes(params.info_hashes).empty())reject("Torrent has no supported info hash.");
                Row row;row.view.directory=destination(directory);row.info=params.info_hashes;row.adding=promise;row.paused=config.paused;row.priorities=config.priorities;row.view.limits=config.limits;row.view.seeding=config.seeding;row.view.metadataOnly=config.metadataOnly;row.view.created=QDateTime::currentSecsSinceEpoch();
                unique(row.info,row.view.directory);prepare(row,params,false);for(const auto& file:row.view.files)if(QFileInfo::exists(QDir(row.view.directory).filePath(file.path)))row.fileProofs[file.path]=QJsonObject{{"external",true}};if(!row.paused)reserveFiles(row);
                id=QUuid::createUuid().toString(QUuid::Id128);row.view.id=id;row.native=nativeBytes(params);persist(row);rows.emplace(id,std::move(row));session->async_add_torrent(std::move(params));
            }catch(...){if(!id.isEmpty()){rows.erase(id);try{if(store)store->erase(id);}catch(...){}}promise->set_exception(std::current_exception());}
        }))promise->set_exception(failure("Torrent engine is shutting down."));
        return future;
    }
    void resolve(std::vector<std::shared_ptr<std::promise<void>>>& pending,std::exception_ptr error={}){
        for(auto& p:pending){if(error)p->set_exception(error);else p->set_value();}pending.clear();
    }
    void save(Row& row,std::shared_ptr<std::promise<void>> promise={}){
        // File priorities include asynchronous part-file migration on the native
        // disk thread. A resume alert alone does not acknowledge that operation.
        if(row.priorityPending){row.saveAfterPriorities=true;if(promise)row.priorityWaiters.push_back(promise);return;}
        if(row.saving){row.saveAgain=true;if(promise)row.nextWaiters.push_back(promise);return;}
        row.saving=true;
        // In the pinned native implementation, flush_disk_cache queues a release
        // job but posts resume data immediately. Wait for the explicit disk-job
        // callback before taking a checkpoint or publishing completion.
        // An unresolved magnet has no native storage object to flush.
        row.flushing=row.view.metadata;
        try{if(row.flushing)row.handle.flush_cache();else row.handle.save_resume_data(lt::torrent_handle::save_info_dict);}
        catch(...){row.saving=false;row.flushing=false;throw;}
        if(promise)row.waiters.push_back(promise);
    }
    void removeNative(Row& row){
        const bool incomplete=!row.view.metadataOnly&&(row.removal==Removal::Cancel||row.removal==Removal::Failure||row.removal==Removal::Keep);
        if(incomplete && row.view.metadata){
            noLinks(row.view.directory);for(const auto& file:row.view.files)noLinks(QDir(row.view.directory).filePath(file.path));
            const auto progress=row.handle.file_progress(lt::torrent_handle::piece_granularity);
            for(auto& file:row.view.files){if(static_cast<std::size_t>(file.index)<progress.size())file.verifiedBytes=progress[static_cast<std::size_t>(file.index)];
                const auto proof=row.fileProofs.value(file.path).toObject();if(proof.value("complete").toBool()&&QFileInfo::exists(QDir(row.view.directory).filePath(file.path)))file.verifiedBytes=file.bytes;
            }
        }
        auto flags=row.removal==Removal::Delete?lt::session::delete_files:incomplete&&row.view.metadata?lt::session::delete_partfile:lt::remove_flags_t{};
        session->remove_torrent(row.handle,flags);
    }
    void saved(Row& row,const lt::add_torrent_params& params){
        row.native=nativeBytes(params);persist(row);row.saving=false;resolve(row.waiters);
        if(row.saveAgain){row.saveAgain=false;row.waiters.swap(row.nextWaiters);save(row);}
        else if(row.removal!=Removal::None)removeNative(row);
        else if(row.completionPending){row.completionPending=false;row.view.state=State::Complete;emit owner.updated(row.view);}
        else if(row.seedingPending){row.seedingPending=false;row.view.state=State::Seeding;row.seedTick=std::chrono::steady_clock::now();emit owner.updated(row.view);}
    }
    void prioritiesApplied(Row& row){
        if(!row.priorityPending)return;
        const auto actual=row.handle.get_file_priorities();
        for(const auto& file:row.view.files){
            const auto index=static_cast<std::size_t>(file.index);
            if(index>=actual.size()||actual[index]!=lt::download_priority_t(static_cast<std::uint8_t>(file.priority)))return;
        }
        row.priorityPending=false;
        row.view.state=row.paused?State::Paused:State::Checking;
        if(row.recheckAfterPriorities){
            // A newly wanted file may receive previously verified boundary bytes
            // from the part file. Re-verify the actual destination after migration;
            // the earlier in-memory piece map is not proof of the moved bytes.
            row.recheckAfterPriorities=false;row.handle.force_recheck();row.view.verifiedBytes=0;
            for(auto& file:row.view.files)file.verifiedBytes=0;
        }
        if(!row.blocked&&!row.view.metadataOnly)row.handle.unset_flags(lt::torrent_flags::upload_mode);
        if(row.saveAfterPriorities){
            row.saveAfterPriorities=false;
            auto waiting=std::move(row.priorityWaiters);row.priorityWaiters.clear();
            if(waiting.empty())save(row);else for(auto& promise:waiting)save(row,std::move(promise));
        }
    }
    void saveFailed(Row& row){
        const auto error=failure("Torrent checkpoint failed. Previous recovery data was retained.");
        row.saving=false;row.flushing=false;row.saveAgain=false;row.completionPending=false;row.seedingPending=false;resolve(row.waiters,error);resolve(row.nextWaiters,error);row.view.error="Torrent checkpoint failed. Previous recovery data was retained.";
        row.paused=true;row.handle.pause();if(row.removing){row.removing->set_exception(error);row.removing.reset();row.removal=Removal::None;}emit owner.updated(row.view);shutdownError=error;
    }
    void retryFailed(Row& row){
        if(row.view.state!=State::Failed||row.blocked||row.removal!=Removal::None||row.adding||row.native.isEmpty())reject("Torrent is not ready for retry.");
        auto params=lt::read_resume_data(lt::span<char const>(row.native.constData(),row.native.size()));
        prepare(row,params,true);
        // Cleanup may have removed files since the previous native handle.
        // Newly reserved files need fresh physical identities, never old proofs.
        for(const auto& file:row.view.files)if(!QFileInfo::exists(QDir(row.view.directory).filePath(file.path)))row.fileProofs.remove(file.path);
        reserveFiles(row);params.flags|=lt::torrent_flags::paused;
        lt::error_code error;auto handle=session->add_torrent(params,error);if(error)reject("Torrent retry could not attach native storage.");
        row.handle=handle;row.restored=true;row.paused=true;
    }
    template<class F> std::future<void> durable(QString id,F action,bool retry=false){
        auto p=std::make_shared<std::promise<void>>();auto future=p->get_future();
        if(!enqueue([this,id=std::move(id),action=std::move(action),p,retry]{try{healthy();auto found=rows.find(id);if(retry&&found!=rows.end()&&!found->second.handle.is_valid())retryFailed(found->second);auto& row=ready(id);action(row);persist(row);save(row,p);emit owner.updated(row.view);}catch(...){p->set_exception(std::current_exception());}}))p->set_exception(failure("Torrent engine is shutting down."));return future;
    }
    void fail(Row& row,const char* message){
        row.view.state=State::Failed;row.view.error=QString::fromUtf8(message);row.paused=true;
        try{if(row.handle.is_valid())row.handle.pause();persist(row);}catch(...){}
        auto error=failure(message);if(row.adding){row.adding->set_exception(error);row.adding.reset();}resolve(row.waiters,error);resolve(row.nextWaiters,error);resolve(row.priorityWaiters,error);row.saving=false;row.flushing=false;row.saveAgain=false;row.priorityPending=false;row.saveAfterPriorities=false;row.completionPending=false;row.seedingPending=false;
        if(row.removing){row.removing->set_exception(error);row.removing.reset();}
        if(!row.handle.is_valid()&&!row.fileProofs.isEmpty()&&row.removal==Removal::None){row.removal=Removal::Failure;removed(row,true);}
        if(row.handle.is_valid()&&row.removal==Removal::None&&!row.view.files.empty())try{row.removal=Removal::Failure;persist(row);removeNative(row);}catch(...){row.removal=Removal::None;row.view.error+=" Incomplete file cleanup could not finish.";}
        emit owner.updated(row.view);
    }
    void metadata(Row& row){
        const auto info=row.handle.torrent_file();if(!info)return;
        try{
            for(const auto& [id,other]:rows)if(id!=row.view.id&&other.view.state!=State::Cancelled&&overlaps(other.info,info->info_hashes()))reject("Duplicate metadata info hash.");
            row.view.files=files(*info,row.view.directory);row.info=info->info_hashes();row.view.name=text(info->name());row.view.totalBytes=info->size_on_disk();row.view.metadata=true;
            if(row.view.metadataOnly){row.paused=true;row.handle.pause();row.view.state=State::Paused;}
            reserveFiles(row);applyPriorities(row);save(row);emit owner.updated(row.view);
        }catch(...){row.blocked=true;fail(row,"Received torrent metadata has unsupported paths or file selection.");}
    }
    void update(const lt::torrent_status& status){
        auto* row=find(status.handle);if(!row||row->view.state==State::Cancelled||row->view.state==State::Failed||row->adding||row->removal!=Removal::None)return;
        prioritiesApplied(*row);
        auto& view=row->view;
        // Native wanted counters count entire pieces, including boundary bytes
        // belonging to skipped files. Report verified selected-file bytes instead.
        view.verifiedBytes=0;
        if(view.metadata){
            const auto progress=row->handle.file_progress(lt::torrent_handle::piece_granularity);
            for(auto& file:view.files){
                file.verifiedBytes=static_cast<std::size_t>(file.index)<progress.size()?std::clamp<qint64>(progress[static_cast<std::size_t>(file.index)],0,file.bytes):0;
                if(file.verifiedBytes>0){auto proof=row->fileProofs.value(file.path).toObject();if(proof.isEmpty())try{const auto target=QDir(view.directory).filePath(file.path);noLinks(target);proof["identity"]=ownedMedia::identity(target);}catch(...){}if(!proof.isEmpty()){if(file.verifiedBytes==file.bytes)proof["complete"]=true;row->fileProofs[file.path]=proof;}}
                if(file.priority)view.verifiedBytes+=file.verifiedBytes;
            }
        }
        view.downloadBytesPerSecond=status.download_payload_rate;view.uploadBytesPerSecond=status.upload_payload_rate;view.peers=status.num_peers;view.seeds=status.num_seeds;
        view.downloadedBytes=status.all_time_download;view.uploadedBytes=status.all_time_upload;view.availability=status.distributed_copies;
        view.ratio=status.all_time_download>0?static_cast<double>(status.all_time_upload)/static_cast<double>(status.all_time_download):0;
        const auto now=std::chrono::steady_clock::now();
        if(view.state==State::Seeding&&!row->paused)row->seedMilliseconds+=std::chrono::duration_cast<std::chrono::milliseconds>(now-row->seedTick).count();row->seedTick=now;view.seedingSeconds=row->seedMilliseconds/1000;
        if(status.errc){fail(*row,"Torrent transfer failed. Check disk space, permissions and destination availability.");return;}
        const auto previous=view.state;
        const bool checking=status.state==lt::torrent_status::checking_files||status.state==lt::torrent_status::checking_resume_data;
        if(row->completionPending||row->seedingPending||row->priorityPending)view.state=State::Checking;
        else if(row->paused)view.state=previous==State::Complete?State::Complete:State::Paused;
        else if(!status.has_metadata)view.state=State::Metadata;
        else if(checking)view.state=State::Checking;
        else if(status.is_finished&&view.verifiedBytes==view.selectedBytes){
            const auto policy=view.seeding;
            const bool stop=policy.mode==SeedMode::Stop||(policy.mode==SeedMode::Ratio&&view.ratio>=policy.ratio)||(policy.mode==SeedMode::Time&&view.seedingSeconds>=policy.seconds);
            if(stop){row->paused=true;row->completionPending=true;view.state=State::Checking;view.downloadBytesPerSecond=view.uploadBytesPerSecond=0;row->handle.pause();save(*row);}
            else if(previous==State::Seeding)view.state=State::Seeding;
            else{row->seedingPending=true;view.state=State::Checking;save(*row);}
        }else view.state=State::Downloading;
        emit owner.updated(view);
    }
    void removed(Row& row,bool deleted){
        const bool incomplete=!row.view.metadataOnly&&(row.removal==Removal::Cancel||row.removal==Removal::Failure||row.removal==Removal::Keep);
        if(incomplete)for(auto& file:row.view.files)if(row.fileProofs.value(file.path).toObject().value("complete").toBool())file.verifiedBytes=file.bytes;
        if((row.removal==Removal::Delete||(incomplete&&row.view.metadata))&&!deleted)return;
        if(incomplete)try{
            for(auto& file:row.view.files)if(file.verifiedBytes<file.bytes){
                const auto target=QDir(row.view.directory).filePath(file.path);noLinks(target);if(row.fileProofs.value(file.path).toObject().value("external").toBool())continue;const auto identity=row.fileProofs.value(file.path).toObject().value("identity").toString();
                if(QFileInfo::exists(target)&&identity.isEmpty())reject("Incomplete torrent file ownership is unavailable; it was retained for review.");
                ownedMedia::erase(std::filesystem::path(target.toStdWString()),0,false,identity);file.verifiedBytes=0;
                auto parent=QFileInfo(target).absolutePath();while(parent!=row.view.directory&&parent.startsWith(row.view.directory+"/",Qt::CaseInsensitive)&&QDir().rmdir(parent))parent=QFileInfo(parent).absolutePath();
            }
            row.view.verifiedBytes=0;for(const auto& file:row.view.files)if(file.priority)row.view.verifiedBytes+=file.verifiedBytes;
        }catch(const std::exception& error){row.view.error+=" Incomplete file cleanup: "+QString::fromUtf8(error.what());row.handle={};row.removal=Removal::None;persist(row);if(row.removing){row.removing->set_exception(std::current_exception());row.removing.reset();}emit owner.updated(row.view);return;}
        const auto id=row.view.id;auto promise=row.removing;
        if(row.removal==Removal::Cancel||row.removal==Removal::Failure){row.handle={};row.removal=Removal::None;persist(row);if(promise)promise->set_value();row.removing.reset();emit owner.updated(row.view);}
        else if(row.removal!=Removal::None){if(store)store->erase(id);rows.erase(id);if(promise)promise->set_value();}
    }
    void alerts(){
        std::vector<lt::alert*> alerts;session->pop_alerts(&alerts);
        for(auto* alert:alerts){
            if(auto* added=lt::alert_cast<lt::add_torrent_alert>(alert)){
                if(auto* row=find(added->params.info_hashes);row&&row->adding){
                    if(added->error)fail(*row,"Torrent could not be added (duplicate or resource failure).");
                    else{row->handle=added->handle;row->view.state=row->paused?State::Paused:(row->view.metadata?State::Checking:State::Metadata);row->adding->set_value(row->view.id);row->adding.reset();if(drainingFlag){row->handle.pause();save(*row);}emit owner.updated(row->view);}
                }
            }else if(auto* received=lt::alert_cast<lt::metadata_received_alert>(alert)){if(auto* row=find(received->handle))metadata(*row);}
            else if(auto* priority=lt::alert_cast<lt::file_prio_alert>(alert)){if(auto* row=find(priority->handle))prioritiesApplied(*row);}
            else if(auto* disk=lt::alert_cast<lt::file_error_alert>(alert)){if(auto* row=find(disk->handle))fail(*row,"Torrent disk operation failed. Incomplete data cleanup will be attempted.");}
            else if(auto* state=lt::alert_cast<lt::state_update_alert>(alert)){for(const auto& item:state->status)update(item);}
            else if(auto* flushed=lt::alert_cast<lt::cache_flushed_alert>(alert)){if(auto* row=find(flushed->handle);row&&row->saving&&row->flushing){row->flushing=false;row->handle.save_resume_data(lt::torrent_handle::save_info_dict);}}
            else if(auto* rd=lt::alert_cast<lt::save_resume_data_alert>(alert)){if(auto* row=find(rd->handle);row&&row->saving)saved(*row,rd->params);}
            else if(auto* unsaved=lt::alert_cast<lt::save_resume_data_failed_alert>(alert)){if(auto* row=find(unsaved->handle);row&&row->saving)saveFailed(*row);}
            else if(auto* deleted=lt::alert_cast<lt::torrent_deleted_alert>(alert)){if(auto* row=find(deleted->info_hashes))removed(*row,true);}
            else if(auto* deletion=lt::alert_cast<lt::torrent_delete_failed_alert>(alert)){if(auto* row=find(deletion->info_hashes))fail(*row,"Torrent data deletion failed. The record and remaining files were retained.");}
            else if(auto* removal=lt::alert_cast<lt::torrent_removed_alert>(alert)){if(auto* row=find(removal->info_hashes))removed(*row,false);}
            else if(auto* error=lt::alert_cast<lt::torrent_error_alert>(alert)){if(auto* row=find(error->handle))fail(*row,"Torrent transfer failed. Check destination access and available space.");}
            else if(lt::alert_cast<lt::alerts_dropped_alert>(alert))reject("Torrent alert queue overflow; session stopped safely.");
        }
    }
    bool pending(){for(const auto& [id,row]:rows)if(row.adding||row.saving||row.removing||row.priorityPending)return true;return false;}
    void run()noexcept{
        try{
            lt::settings_pack settings;
            // TCP's random ephemeral port can fall inside Windows UDP reserved
            // ranges. Native TCP/UDP listeners need the same port; start with
            // the conventional torrent port and let Rasterbar retry collisions.
            settings.set_str(lt::settings_pack::listen_interfaces,options.loopbackOnly?"127.0.0.1:6881":"0.0.0.0:6881,[::]:6881");
            settings.set_bool(lt::settings_pack::enable_dht,options.discovery&&!options.loopbackOnly);settings.set_bool(lt::settings_pack::enable_lsd,options.discovery&&!options.loopbackOnly);
            settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);
            if(options.loopbackOnly){
                settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"");settings.set_int(lt::settings_pack::min_reconnect_time,1);
                settings.set_bool(lt::settings_pack::enable_outgoing_utp,false);settings.set_bool(lt::settings_pack::enable_incoming_utp,false);
                settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);
            }
            settings.set_int(lt::settings_pack::connections_limit,64);settings.set_int(lt::settings_pack::active_limit,maxTorrents);settings.set_int(lt::settings_pack::max_metadata_size,maxMetadata);
            // Explicit flush_cache, resume and delete responses are posted even
            // without storage subscription. Suppress automatic cache-flush
            // notifications so only our one outstanding flush can acknowledge
            // its checkpoint; priorities are polled after native disk migration.
            settings.set_int(lt::settings_pack::alert_queue_size,2048);settings.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::status);settings.set_int(lt::settings_pack::stop_tracker_timeout,1);
            lt::session_params parameters(settings);
#ifdef DOWNSHIFT_TORRENT_FAULT_TEST
            parameters.disk_io_constructor=testing::faultDisk();
#endif
            session=std::make_unique<lt::session>(std::move(parameters));session->set_alert_notify([this]{wake=true;condition.notify_one();});
            // Put LAN/loopback peers in the global class too, so saved caps apply
            // to every peer instead of being bypassed by native local defaults.
            lt::ip_filter classes;
            classes.add_rule(lt::make_address("0.0.0.0"),lt::make_address("255.255.255.255"),1);
            classes.add_rule(lt::make_address("::"),lt::make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),1);
            session->set_peer_class_filter(classes);
            if(!options.databasePath.isEmpty()){store=std::make_unique<TorrentStore>(options.databasePath);options.databasePath=store->path();}restore();if(store)store->backup();
        }catch(const std::exception& e){fatal=QString::fromUtf8(e.what());emit owner.engineFailed(fatal);}
        catch(...){fatal="Torrent session startup failed.";emit owner.engineFailed(fatal);}
        auto next=std::chrono::steady_clock::now(),periodic=next+std::chrono::seconds(15),deadline=next;
        bool draining=false;
        for(;;){
            std::deque<std::function<void()>> tasks;bool end;
            {std::unique_lock lock(mutex);condition.wait_until(lock,next,[this]{return (closing&&!drainingFlag)||!commands.empty()||wake.exchange(false);});tasks.swap(commands);end=closing;}
            for(auto& task:tasks)task();
            if(end&&!draining){
                draining=true;drainingFlag=true;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
                if(fatal.isEmpty())try{for(auto& [id,row]:rows)if(row.handle.is_valid()&&!row.adding&&row.view.state!=State::Cancelled&&row.removal==Removal::None){row.handle.pause();save(row);}}catch(...){shutdownError=std::current_exception();}
            }
            if(fatal.isEmpty())try{
                alerts();const auto now=std::chrono::steady_clock::now();
                if(draining)for(auto& [id,row]:rows)if(row.priorityPending&&row.handle.is_valid())prioritiesApplied(row);
                if(now>=next){
                    // Dirty-only native updates can omit an idle seed. Poll copied
                    // status on the worker so time policies still advance offline.
                    if(!draining)for(auto& [id,row]:rows)if(row.handle.is_valid()&&!row.adding&&row.removal==Removal::None)update(row.handle.status(lt::torrent_handle::query_distributed_copies));
                    next=now+std::chrono::milliseconds(draining?20:500);
                }
                if(!draining&&store&&now>=periodic){for(auto& [id,row]:rows)if(row.handle.is_valid()&&!row.adding&&!row.saving&&row.removal==Removal::None&&row.view.state!=State::Cancelled&&row.view.state!=State::Failed)save(row);periodic=now+std::chrono::seconds(15);}
            }catch(...){
                shutdownError=std::current_exception();fatal="Torrent persistence/session processing failed; saved files were retained.";emit owner.engineFailed(fatal);
                for(auto& [id,row]:rows)fail(row,"Torrent session stopped after an internal or persistence error.");
            }
            if(!fatal.isEmpty())next=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            if(draining&&(!pending()||!fatal.isEmpty()||std::chrono::steady_clock::now()>=deadline))break;
        }
        if(pending()&&!shutdownError)shutdownError=failure("Torrent shutdown checkpoint timed out; previous checkpoints were retained.");
        for(auto& [id,row]:rows){if(row.adding){row.adding->set_exception(failure("Torrent add interrupted by shutdown."));row.adding.reset();}resolve(row.waiters,failure("Torrent shutdown interrupted checkpoint."));resolve(row.nextWaiters,failure("Torrent shutdown interrupted checkpoint."));resolve(row.priorityWaiters,failure("Torrent shutdown interrupted selection."));if(row.removing){row.removing->set_exception(failure("Torrent shutdown interrupted removal."));row.removing.reset();}}
        try{if(store&&fatal.isEmpty())store->backup();}catch(...){shutdownError=std::current_exception();}
        try{if(session){session->set_alert_notify({});auto proxy=session->abort();session.reset();}}catch(...){session.reset();}
        store.reset();if(shutdownError)stopped.set_exception(shutdownError);else stopped.set_value();
    }
};
TorrentEngine::TorrentEngine(Options options,QObject* parent):QObject(parent){qRegisterMetaType<Snapshot>();impl_=std::make_unique<Impl>(*this,std::move(options));}
TorrentEngine::~TorrentEngine()=default;
std::future<QString> TorrentEngine::addFile(QString file,QString directory){return addFile(std::move(file),std::move(directory),{});}
std::future<QString> TorrentEngine::addMagnet(QString uri,QString directory){return addMagnet(std::move(uri),std::move(directory),{});}
std::future<QString> TorrentEngine::addFile(QString file,QString directory,AddOptions options){return impl_->add(std::move(file),std::move(directory),false,std::move(options));}
std::future<QString> TorrentEngine::addMagnet(QString uri,QString directory,AddOptions options){return impl_->add(std::move(uri),std::move(directory),true,std::move(options));}
std::future<std::vector<Snapshot>> TorrentEngine::snapshots(){return impl_->submit([this]{std::vector<Snapshot> result;for(const auto& [id,row]:impl_->rows)result.push_back(row.view);return result;});}
std::future<void> TorrentEngine::checkpoint(QString id){return impl_->durable(std::move(id),[](auto&){});}
std::future<void> TorrentEngine::pause(QString id){return impl_->durable(std::move(id),[](auto& row){row.paused=true;row.completionPending=false;row.seedingPending=false;row.view.state=State::Paused;row.handle.pause();row.view.downloadBytesPerSecond=row.view.uploadBytesPerSecond=0;});}
std::future<void> TorrentEngine::resume(QString id){return impl_->durable(std::move(id),[this](auto& row){impl_->reserveFiles(row);if(row.view.metadataOnly&&row.view.metadata)reject("Confirm the torrent file selection before starting.");row.paused=false;row.completionPending=false;row.seedingPending=false;row.view.error.clear();row.view.state=row.view.metadata?State::Checking:State::Metadata;row.handle.clear_error();if(row.restored){row.handle.force_recheck();row.restored=false;}row.handle.resume();},true);}
std::future<void> TorrentEngine::startSelected(QString id,bool paused){return impl_->durable(std::move(id),[this,paused](auto& row){if(!row.view.metadata)reject("Wait for torrent metadata before confirming files.");row.view.metadataOnly=false;impl_->reserveFiles(row);row.paused=paused;row.completionPending=false;row.seedingPending=false;row.view.error.clear();row.view.state=paused?State::Paused:State::Checking;row.handle.clear_error();row.handle.unset_flags(lt::torrent_flags::upload_mode);if(row.restored){row.handle.force_recheck();row.restored=false;}if(paused)row.handle.pause();else row.handle.resume();});}
std::future<void> TorrentEngine::configure(QString id,Limits limits,SeedPolicy policy){return impl_->durable(std::move(id),[limits,policy](auto& row){validate(limits);validate(policy);row.view.limits=limits;row.view.seeding=policy;row.handle.set_download_limit(limits.download);row.handle.set_upload_limit(limits.upload);row.handle.set_max_connections(limits.connections);});}
std::future<void> TorrentEngine::selectFiles(QString id,std::vector<int> priorities){return impl_->durable(std::move(id),[this,priorities=std::move(priorities)](auto& row){validate(priorities);if(row.priorityPending)reject("A torrent file selection update is still pending.");if(!row.view.metadata)reject("Wait for metadata before choosing files.");if(priorities.size()!=row.view.files.size())reject("File priorities do not match the torrent file list.");for(std::size_t n=0;n<priorities.size();++n)if(row.priorities[n]==0&&priorities[n]>0)row.recheckAfterPriorities=true;row.priorities=priorities;row.completionPending=false;row.seedingPending=false;row.view.state=row.paused?State::Paused:State::Checking;for(std::size_t n=0;n<row.view.files.size();++n)row.view.files[n].priority=row.priorities[n];impl_->reserveFiles(row);impl_->applyPriorities(row);});}
std::future<void> TorrentEngine::setSessionLimits(Limits limits){return impl_->submit([this,limits]{validate(limits);if(impl_->store)impl_->store->settings(QJsonDocument(QJsonObject{{"version",1},{"limits",encode(limits)}}).toJson(QJsonDocument::Compact));lt::settings_pack settings;settings.set_int(lt::settings_pack::download_rate_limit,limits.download);settings.set_int(lt::settings_pack::upload_rate_limit,limits.upload);settings.set_int(lt::settings_pack::connections_limit,limits.connections);impl_->session->apply_settings(settings);impl_->global=limits;});}
std::future<Limits> TorrentEngine::sessionLimits(){return impl_->submit([this]{return impl_->global;});}
std::future<quint16> TorrentEngine::listeningPort(){return impl_->submit([this]{return impl_->session->listen_port();});}
std::future<Details> TorrentEngine::details(QString id){return impl_->submit([this,id=std::move(id)]{
    auto& row=impl_->ready(id);Details result;std::vector<lt::peer_info> peers;row.handle.get_peer_info(peers);
    for(const auto& peer:peers)result.peers.push_back({QString::fromStdString(peer.remote_endpoint().address().to_string())+":"+QString::number(peer.remote_endpoint().port()),text(peer.client),peer.down_speed,peer.up_speed,static_cast<bool>(peer.flags&lt::peer_info::seed)});
    for(const auto& tracker:row.handle.trackers()){
        Tracker value;value.url=text(tracker.url);value.verified=tracker.verified;
        for(const auto& endpoint:tracker.endpoints)for(const auto& hash:endpoint.info_hashes){value.failures+=hash.fails;if(hash.last_error)value.error="Tracker announce failed.";}
        result.trackers.push_back(std::move(value));
    }
    row.handle.piece_availability(result.availability);const auto progress=row.handle.file_progress(lt::torrent_handle::piece_granularity);
    for(auto& file:row.view.files)if(static_cast<std::size_t>(file.index)<progress.size())file.verifiedBytes=progress[static_cast<std::size_t>(file.index)];return result;
});}
std::future<void> TorrentEngine::remove(QString id,bool deleteData,bool confirmed){
    auto promise=std::make_shared<std::promise<void>>();auto future=promise->get_future();
    if(!impl_->enqueue([this,id=std::move(id),deleteData,confirmed,promise]{Impl::Row* pendingRow=nullptr;try{
        impl_->healthy();if(deleteData&&!confirmed)reject("Deleting torrent data requires explicit confirmation.");
        auto found=impl_->rows.find(id);if(found==impl_->rows.end())reject("Unknown torrent.");auto& row=found->second;pendingRow=&row;if(row.adding||row.removing)reject("Torrent is busy.");
        if(deleteData){noLinks(row.view.directory);if(!row.view.metadata)reject("Cannot delete payload without verified metadata.");for(const auto& file:row.view.files)noLinks(QDir(row.view.directory).filePath(file.path));}
        if(!row.handle.is_valid()){
            if(deleteData){
                // Reattach only a paused native storage handle. No payload transfer
                // or seeding starts while deleting an explicitly confirmed record.
                lt::error_code error;
                auto params=lt::read_resume_data(lt::span<char const>(row.native.constData(),static_cast<std::size_t>(row.native.size())),error);
                if(error)reject("Cancelled torrent recovery data is invalid; files were retained.");
                impl_->prepare(row,params,true);row.handle=impl_->session->add_torrent(std::move(params),error);
                if(error)reject("Cancelled torrent could not be attached for deletion; files were retained.");
                row.removal=Impl::Removal::Delete;row.removing=promise;impl_->persist(row);impl_->removeNative(row);return;
            }
            row.removal=Impl::Removal::Keep;row.removing=promise;impl_->removed(row,true);return;
        }
        row.paused=true;row.handle.pause();row.view.state=State::Cancelled;row.removal=deleteData?Impl::Removal::Delete:Impl::Removal::Keep;row.removing=promise;impl_->persist(row);impl_->save(row);
    }catch(...){if(pendingRow&&pendingRow->removing==promise){pendingRow->removing.reset();pendingRow->removal=Impl::Removal::None;}promise->set_exception(std::current_exception());}}))promise->set_exception(failure("Torrent engine is shutting down."));return future;
}
std::future<void> TorrentEngine::cancel(QString id){
    auto promise=std::make_shared<std::promise<void>>();auto future=promise->get_future();
    if(!impl_->enqueue([this,id=std::move(id),promise]{Impl::Row* pendingRow=nullptr;try{impl_->healthy();auto& row=impl_->ready(id);pendingRow=&row;row.paused=true;row.handle.pause();row.view.state=State::Cancelled;row.view.downloadBytesPerSecond=row.view.uploadBytesPerSecond=0;row.removal=Impl::Removal::Cancel;row.removing=promise;impl_->persist(row);impl_->save(row);}catch(...){if(pendingRow&&pendingRow->removing==promise){pendingRow->removing.reset();pendingRow->removal=Impl::Removal::None;}promise->set_exception(std::current_exception());}}))promise->set_exception(failure("Torrent engine is shutting down."));return future;
}
std::future<void> TorrentEngine::connectPeer(QString id,QString address,quint16 port){return impl_->submit([this,id=std::move(id),address=std::move(address),port]{if(!port)reject("Invalid peer port.");lt::error_code error;const auto ip=lt::make_address(address.toStdString(),error);if(error)reject("Invalid peer address.");impl_->ready(id).handle.connect_peer(lt::tcp::endpoint(ip,port));});}
std::shared_future<void> TorrentEngine::shutdown(){impl_->stop();return impl_->finished;}
}
