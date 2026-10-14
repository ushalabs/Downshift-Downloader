#include "TorrentStore.h"
#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <dpapi.h>
#include <sqlite3.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <stdexcept>
namespace downshift::torrent {
namespace {
constexpr int maxRecord=48*1024*1024;
[[noreturn]] void fail(){throw std::runtime_error("Torrent history is unavailable or damaged. Saved data was preserved.");}
QByteArray protect(const QByteArray& bytes,bool encrypt){
    if(bytes.isEmpty()||bytes.size()>maxRecord)fail();
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))},output{};
    const auto ok=encrypt?CryptProtectData(&input,L"Downshift torrents",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output):CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output);
    if(!ok)fail();QByteArray result(reinterpret_cast<char*>(output.pbData),output.cbData);SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);return result;
}
struct Statement {
    sqlite3_stmt* value=nullptr;
    Statement(sqlite3* db,const char* sql){if(sqlite3_prepare_v2(db,sql,-1,&value,nullptr)!=SQLITE_OK)fail();}
    ~Statement(){sqlite3_finalize(value);}
    void text(const QString& id){const auto bytes=id.toUtf8();if(sqlite3_bind_text(value,1,bytes.constData(),static_cast<int>(bytes.size()),SQLITE_TRANSIENT)!=SQLITE_OK)fail();}
    void blob(int index,const QByteArray& bytes){if(sqlite3_bind_blob(value,index,bytes.constData(),static_cast<int>(bytes.size()),SQLITE_TRANSIENT)!=SQLITE_OK)fail();}
    void done(){if(sqlite3_step(value)!=SQLITE_DONE)fail();}
};
void exec(sqlite3* db,const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)fail();}
QByteArray column(sqlite3_stmt* row,int index){const auto size=sqlite3_column_bytes(row,index);if(size<=0||size>maxRecord)fail();return protect(QByteArray(static_cast<const char*>(sqlite3_column_blob(row,index)),size),false);}
}
TorrentStore::TorrentStore(const QString& input){
    if(!QDir::isAbsolutePath(input)||QFileInfo(input).fileName().compare("queue.sqlite",Qt::CaseInsensitive)==0)fail();
    if(!QDir().mkpath(QFileInfo(input).absolutePath()))fail();
    for(QString part=QFileInfo(input).absoluteFilePath();;){const QFileInfo info(part);if(info.isSymLink()||info.isJunction())fail();const auto parent=info.absolutePath();if(parent==part)break;part=parent;}
    path_=QDir(QFileInfo(input).absolutePath()).canonicalPath()+"/"+QFileInfo(input).fileName();
    lease_=std::make_unique<QLockFile>(path_+".lock");lease_->setStaleLockTime(0);if(!lease_->tryLock())throw std::runtime_error("Torrent history is already open in another session.");
    const QFileInfo live(path_);const bool fresh=!live.exists()||live.size()==0;
    if((QFileInfo::exists(path_+".owned")||QFileInfo::exists(path_+".backup.sqlite"))&&fresh)fail();
    if(sqlite3_open_v2(path_.toUtf8().constData(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){sqlite3_close(db_);db_=nullptr;fail();}
    try{
        const auto nativePath=QDir::toNativeSeparators(path_).toStdWString();
        const auto file=CreateFileW(nativePath.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)fail();
        FILE_ID_INFO physical{};const auto identified=GetFileInformationByHandleEx(file,FileIdInfo,&physical,sizeof(physical));CloseHandle(file);if(!identified)fail();
        QByteArray identityBytes(reinterpret_cast<const char*>(&physical.VolumeSerialNumber),sizeof(physical.VolumeSerialNumber));identityBytes.append(reinterpret_cast<const char*>(physical.FileId.Identifier),sizeof(physical.FileId.Identifier));
        const auto identity=QString::fromLatin1(QCryptographicHash::hash(identityBytes,QCryptographicHash::Sha256).toHex());
        QFile previous(path_+".owned");if(previous.exists()){
            if(!previous.open(QIODevice::ReadOnly)||previous.size()>4096)fail();const auto marker=QJsonDocument::fromJson(previous.readAll()).object();
            if(marker.value("version").toInt()!=1||marker.value("identity").toString()!=identity)throw std::runtime_error("Torrent store physical identity changed. Existing history and backups were preserved.");
            previous.close();
        }
        const auto mutexName=("Local\\Downshift.TorrentStore."+identity).toStdWString();
        const auto physicalLease=CreateMutexW(nullptr,TRUE,mutexName.c_str());const auto leaseError=GetLastError();
        if(!physicalLease)fail();if(leaseError==ERROR_ALREADY_EXISTS){CloseHandle(physicalLease);throw std::runtime_error("This physical torrent store is already open.");}identityLease_=physicalLease;
        sqlite3_busy_timeout(db_,2000);
        {Statement application(db_,"PRAGMA application_id");if(sqlite3_step(application.value)!=SQLITE_ROW)fail();const auto id=sqlite3_column_int(application.value,0);
        // Do not migrate an unrelated SQLite file, even if it has been renamed.
        Statement tables(db_,"SELECT count(*) FROM sqlite_master WHERE type='table'");if(sqlite3_step(tables.value)!=SQLITE_ROW||(id!=0x44535452&&(id!=0||sqlite3_column_int(tables.value,0)!=0)))fail();}
        {Statement check(db_,"PRAGMA quick_check");if(sqlite3_step(check.value)!=SQLITE_ROW||QByteArray(reinterpret_cast<const char*>(sqlite3_column_text(check.value,0)))!="ok")fail();}
        {Statement version(db_,"PRAGMA user_version");if(sqlite3_step(version.value)!=SQLITE_ROW||sqlite3_column_int(version.value,0)>1)fail();}
        exec(db_,"PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA secure_delete=ON; BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS torrents(id TEXT PRIMARY KEY,payload BLOB NOT NULL); CREATE TABLE IF NOT EXISTS torrent_settings(id INTEGER PRIMARY KEY CHECK(id=1),payload BLOB NOT NULL); CREATE TABLE IF NOT EXISTS torrent_meta(id INTEGER PRIMARY KEY CHECK(id=1),payload BLOB NOT NULL); PRAGMA application_id=1146311762; PRAGMA user_version=1;");
        if(fresh){Statement marker(db_,"INSERT INTO torrent_meta VALUES(1,?1)");marker.blob(1,protect("{\"version\":1,\"empty\":true}",true));marker.done();}
        exec(db_,"COMMIT");
        QSaveFile marker(path_+".owned");const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"identity",identity}}).toJson(QJsonDocument::Compact);
        if(!marker.open(QIODevice::WriteOnly)||marker.write(bytes)!=bytes.size()||!marker.commit())fail();
    }catch(...){sqlite3_close(db_);db_=nullptr;if(identityLease_){ReleaseMutex(identityLease_);CloseHandle(identityLease_);identityLease_=nullptr;}throw;}
}
TorrentStore::~TorrentStore(){sqlite3_close(db_);if(identityLease_){ReleaseMutex(identityLease_);CloseHandle(identityLease_);}}
std::vector<std::pair<QString,QByteArray>> TorrentStore::load(){
    Statement query(db_,"SELECT id,payload FROM torrents ORDER BY rowid");std::vector<std::pair<QString,QByteArray>> rows;int code;
    while((code=sqlite3_step(query.value))==SQLITE_ROW){if(rows.size()>=8)fail();rows.emplace_back(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(query.value,0))),column(query.value,1));}if(code!=SQLITE_DONE)fail();
    Statement marker(db_,"SELECT payload FROM torrent_meta WHERE id=1");if(sqlite3_step(marker.value)!=SQLITE_ROW)fail();
    const auto expected=QJsonDocument::fromJson(column(marker.value,0)).object();
    if(expected.value("version").toInt()!=1||!expected.value("empty").isBool()||expected.value("empty").toBool()!=rows.empty())throw std::runtime_error("Torrent history does not match its encrypted acceptance marker. No replacement empty history was accepted.");
    return rows;
}
void TorrentStore::save(const QString& id,const QByteArray& record){
    exec(db_,"BEGIN IMMEDIATE");
    try{
        Statement query(db_,"INSERT INTO torrents(id,payload) VALUES(?1,?2) ON CONFLICT(id) DO UPDATE SET payload=excluded.payload");query.text(id);query.blob(2,protect(record,true));query.done();
        Statement marker(db_,"INSERT OR REPLACE INTO torrent_meta VALUES(1,?1)");marker.blob(1,protect("{\"version\":1,\"empty\":false}",true));marker.done();exec(db_,"COMMIT");
    }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void TorrentStore::erase(const QString& id){
    exec(db_,"BEGIN IMMEDIATE");
    try{
        Statement query(db_,"DELETE FROM torrents WHERE id=?1");query.text(id);query.done();
        Statement count(db_,"SELECT count(*) FROM torrents");if(sqlite3_step(count.value)!=SQLITE_ROW)fail();const bool empty=sqlite3_column_int64(count.value,0)==0;
        Statement marker(db_,"INSERT OR REPLACE INTO torrent_meta VALUES(1,?1)");marker.blob(1,protect(QJsonDocument(QJsonObject{{"version",1},{"empty",empty}}).toJson(QJsonDocument::Compact),true));marker.done();exec(db_,"COMMIT");
    }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
QByteArray TorrentStore::settings(){Statement query(db_,"SELECT payload FROM torrent_settings WHERE id=1");const auto code=sqlite3_step(query.value);if(code==SQLITE_DONE)return {};if(code!=SQLITE_ROW)fail();return column(query.value,0);}
void TorrentStore::settings(const QByteArray& record){Statement query(db_,"INSERT INTO torrent_settings VALUES(1,?1) ON CONFLICT(id) DO UPDATE SET payload=excluded.payload");query.blob(1,protect(record,true));query.done();}
void TorrentStore::backup(){
    const auto temporary=path_+".backup.pending";sqlite3* copy=nullptr;
    if(sqlite3_open_v2(temporary.toUtf8().constData(),&copy,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK){sqlite3_close(copy);fail();}
    auto* operation=sqlite3_backup_init(copy,"main",db_,"main");const auto code=operation?sqlite3_backup_step(operation,-1):SQLITE_ERROR;const auto ended=operation?sqlite3_backup_finish(operation):SQLITE_ERROR;
    const auto flat=sqlite3_exec(copy,"PRAGMA journal_mode=DELETE;",nullptr,nullptr,nullptr);const auto closed=sqlite3_close(copy);
    if(code!=SQLITE_DONE||ended!=SQLITE_OK||flat!=SQLITE_OK||closed!=SQLITE_OK)fail();
    // QSaveFile replaces only our managed backup, never the live database.
    QFile input(temporary);if(!input.open(QIODevice::ReadOnly))fail();QSaveFile output(path_+".backup.sqlite");if(!output.open(QIODevice::WriteOnly))fail();
    while(!input.atEnd()){const auto bytes=input.read(1024*1024);if(bytes.isEmpty()||output.write(bytes)!=bytes.size())fail();}if(!output.commit())fail();input.close();QFile::remove(temporary);
}
}
