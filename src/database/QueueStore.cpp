#include "QueueStore.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dpapi.h>
#include <sqlite3.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QUuid>
#include <QRegularExpression>
#include <memory>
#include <stdexcept>
namespace downshift {
namespace {
[[noreturn]] void failure() { throw std::runtime_error("The download database is unavailable or damaged. Your files have been preserved."); }
QByteArray protect(const QByteArray& bytes, bool encrypt) {
    if (bytes.isEmpty() || bytes.size() > 256 * 1024) failure();
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))}, output{};
    const auto ok = encrypt ? CryptProtectData(&input, L"Downshift queue", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) failure();
    const QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
    return result;
}
struct Statement {
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db, const char* sql) { if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK) failure(); }
    ~Statement() { sqlite3_finalize(value); }
    void text(int n, const QString& text) { const auto bytes = text.toUtf8(); if (sqlite3_bind_text(value, n, bytes.constData(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT) != SQLITE_OK) failure(); }
    void blob(int n, const QByteArray& data) { if (sqlite3_bind_blob(value, n, data.constData(), static_cast<int>(data.size()), SQLITE_TRANSIENT) != SQLITE_OK) failure(); }
    void done() { if (sqlite3_step(value) != SQLITE_DONE) failure(); }
};
void exec(sqlite3* db, const char* sql) { if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) failure(); }
QByteArray column(sqlite3_stmt* s) {
    const auto count = sqlite3_column_bytes(s, 0);
    if (count <= 0 || count > 256 * 1024) failure();
    return protect(QByteArray(static_cast<const char*>(sqlite3_column_blob(s, 0)), count), false);
}
}
QueueStore::QueueStore(const QString& path):path_(QFileInfo(path).absoluteFilePath()) {
    const QFileInfo live(path_);
    if(QFileInfo::exists(QDir(path_+".backups").filePath("owned.json")) && (!live.exists() || live.size()==0))
        throw std::runtime_error("The saved history file is missing or empty. Its encrypted backups were preserved. Open the data folder in Settings; do not replace it with a new empty queue.");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) failure();
    // A named Win32 lease in QueueManager prevents competing owners of this database.
    if (sqlite3_open_v2(path.toUtf8().constData(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(db_); db_ = nullptr; failure();
    }
    try {
        sqlite3_busy_timeout(db_, 2000);
        exec(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA secure_delete=ON;");
        Statement version(db_, "PRAGMA user_version");
        if (sqlite3_step(version.value) != SQLITE_ROW || sqlite3_column_int(version.value, 0) > 2) failure();
        exec(db_, "BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS downloads(id TEXT PRIMARY KEY, payload BLOB NOT NULL);"
            "CREATE TABLE IF NOT EXISTS settings(id INTEGER PRIMARY KEY CHECK(id=1), payload BLOB NOT NULL);"
            "CREATE TABLE IF NOT EXISTS handoffs(id TEXT PRIMARY KEY, payload BLOB NOT NULL); PRAGMA user_version=2; COMMIT;");
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
QueueStore::~QueueStore() { sqlite3_close(db_); }
std::vector<QByteArray> QueueStore::load() {
    Statement s(db_, "SELECT payload FROM downloads ORDER BY rowid"); std::vector<QByteArray> result;
    int status;
    while ((status = sqlite3_step(s.value)) == SQLITE_ROW) { if (result.size() >= 10000) failure(); result.push_back(column(s.value)); }
    if (status != SQLITE_DONE) failure(); return result;
}
void QueueStore::save(const QString& id, const QByteArray& json) {
    Statement s(db_, "INSERT INTO downloads(id,payload) VALUES(?1,?2) ON CONFLICT(id) DO UPDATE SET payload=excluded.payload");
    s.text(1, id); s.blob(2, protect(json, true)); s.done();
}
void QueueStore::erase(const QString& id) { Statement s(db_, "DELETE FROM downloads WHERE id=?1"); s.text(1, id); s.done(); }
QByteArray QueueStore::settings() { Statement s(db_, "SELECT payload FROM settings WHERE id=1"); const int status = sqlite3_step(s.value); if (status == SQLITE_DONE) return {}; if (status != SQLITE_ROW) failure(); return column(s.value); }
void QueueStore::settings(const QByteArray& json) { Statement s(db_, "INSERT INTO settings VALUES(1,?1) ON CONFLICT(id) DO UPDATE SET payload=excluded.payload"); s.blob(1, protect(json, true)); s.done(); }
std::vector<QByteArray> QueueStore::receipts() {
    Statement s(db_,"SELECT payload FROM handoffs ORDER BY rowid");std::vector<QByteArray> result;int status;
    while((status=sqlite3_step(s.value))==SQLITE_ROW){if(result.size()>=10000)failure();result.push_back(column(s.value));}
    if(status!=SQLITE_DONE)failure();return result;
}
void QueueStore::handoff(const QString& id,const QByteArray& entry,const QString& key,const QByteArray& receipt) {
    exec(db_,"BEGIN IMMEDIATE");
    try { save(id,entry);Statement s(db_,"INSERT INTO handoffs(id,payload) VALUES(?1,?2) ON CONFLICT(id) DO UPDATE SET payload=excluded.payload");s.text(1,key);s.blob(2,protect(receipt,true));s.done();exec(db_,"COMMIT"); }
    catch(...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw; }
}
void QueueStore::backup() {
    const auto folder=path_+".backups";
    if(!QDir().mkpath(folder))throw std::runtime_error("History is saved, but its backup folder is unavailable.");
    const auto indexPath=QDir(folder).filePath("owned.json");QJsonArray owned;
    QFile index(indexPath);
    if(index.exists()) {
        if(!index.open(QIODevice::ReadOnly) || index.size()>8192)throw std::runtime_error("History is saved, but its backup index is unavailable.");
        const auto document=QJsonDocument::fromJson(index.readAll());
        if(!document.isObject() || document.object().value("version").toInt()!=1)throw std::runtime_error("History is saved, but its backup index is invalid.");
        owned=document.object().value("files").toArray();
        const QRegularExpression name("^snapshot-[0-9a-f]{32}\\.sqlite$");
        if(owned.size()>8)throw std::runtime_error("History is saved, but its backup index is invalid.");
        for(const auto& item:owned)if(!name.match(item.toString()).hasMatch())throw std::runtime_error("History is saved, but its backup index is invalid.");
        index.close();
    }
    const auto name="snapshot-"+QUuid::createUuid().toString(QUuid::Id128)+".sqlite";
    const auto target=QDir(folder).filePath(name),temporary=target+".pending";
    sqlite3* copy=nullptr;
    if(sqlite3_open_v2(temporary.toUtf8().constData(),&copy,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK){sqlite3_close(copy);throw std::runtime_error("History is saved, but a backup could not be created.");}
    auto* operation=sqlite3_backup_init(copy,"main",db_,"main");
    const auto copied=operation ? sqlite3_backup_step(operation,-1):SQLITE_ERROR;
    const auto finished=operation ? sqlite3_backup_finish(operation):SQLITE_ERROR;
    const auto flattened=sqlite3_exec(copy,"PRAGMA journal_mode=DELETE;",nullptr,nullptr,nullptr);
    const auto closed=sqlite3_close(copy);
    if(copied!=SQLITE_DONE || finished!=SQLITE_OK || flattened!=SQLITE_OK || closed!=SQLITE_OK || !QFile::rename(temporary,target)){
        QFile::remove(temporary);throw std::runtime_error("History is saved, but its backup could not be verified.");
    }
    owned.append(name);QString expired;if(owned.size()>8){expired=owned.first().toString();owned.removeFirst();}
    QSaveFile save(indexPath);
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"files",owned}}).toJson(QJsonDocument::Compact);
    if(!save.open(QIODevice::WriteOnly) || save.write(bytes)!=bytes.size() || !save.commit())throw std::runtime_error("History is saved, but its backup index could not be saved.");
    // Only names recorded in the validated ownership index are ever rotated.
    if(!expired.isEmpty())QFile::remove(QDir(folder).filePath(expired));
}
}
