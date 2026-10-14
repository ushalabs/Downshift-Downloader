#include "StorageLocation.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <stdexcept>
namespace downshift {
QString storageIdentity(const QString& file) {
    const auto name=file.toStdWString();const auto handle=CreateFileW(name.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return {};
    BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(handle,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));CloseHandle(handle);
    if(!ok)return {};
    const auto key=QString("%1:%2:%3").arg(info.dwVolumeSerialNumber).arg(info.nFileIndexHigh).arg(info.nFileIndexLow).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex().left(32));
}
QString storageDirectory(const QString& applicationDirectory,const QString& userSid) {
    QFile binding(QDir(applicationDirectory).filePath("downshift-storage.json"));
    if(binding.exists()) {
        if(!binding.open(QIODevice::ReadOnly) || binding.size()>8192)throw std::runtime_error("The saved-history location binding cannot be read. No replacement queue was created.");
        QJsonParseError error;const auto document=QJsonDocument::fromJson(binding.readAll(),&error);const auto object=document.object();
        const auto configured=object.value("dataDirectory").toString();const auto expected=object.value("expectedIdentity").toString();
        if(error.error!=QJsonParseError::NoError || !document.isObject() || object.value("version").toInt()!=1 || configured.isEmpty() || configured.size()>32768 || configured.contains(QChar(0)) ||
            !QRegularExpression("^S-1-[0-9]+(?:-[0-9]+)+$").match(userSid).hasMatch() ||
            (!expected.isEmpty() && !QRegularExpression("^[0-9a-f]{32}$").match(expected).hasMatch()))
            throw std::runtime_error("The saved-history location binding is invalid. Existing data was preserved.");
        const auto root=QDir::cleanPath(QDir(applicationDirectory).absoluteFilePath(configured));
        const auto directory=QDir(root).filePath("users/"+userSid);
        if(!expected.isEmpty() && storageIdentity(QDir(directory).filePath("queue.sqlite"))!=expected)
            throw std::runtime_error("This launch opened a different saved-history file than the configured one. No empty replacement was created. Check the data-location binding.");
        return directory;
    }
    PWSTR known=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&known)))throw std::runtime_error("Windows could not locate your saved download history.");
    const auto directory=QDir(QString::fromWCharArray(known)).filePath("Usha Labs/Downshift");CoTaskMemFree(known);return directory;
}
}
