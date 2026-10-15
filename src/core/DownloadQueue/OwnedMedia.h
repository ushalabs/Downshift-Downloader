#pragma once
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <cstring>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace downshift::ownedMedia {
inline QString workspace(const QString& folder,const QString& id) { return QDir(folder).filePath(".downshift-video-"+id); }
struct Handle { HANDLE value=INVALID_HANDLE_VALUE; ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
inline void fail(){throw std::runtime_error("Video staging cleanup failed. Close applications using its files and remove the task again.");}
inline QString identity(const BY_HANDLE_FILE_INFORMATION& info){return QString("%1:%2:%3").arg(info.dwVolumeSerialNumber).arg(info.nFileIndexHigh).arg(info.nFileIndexLow);}
inline QString identity(const QString& directory){Handle item{CreateFileW(std::filesystem::path(directory.toStdWString()).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr)};BY_HANDLE_FILE_INFORMATION info{};if(item.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(item.value,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))fail();return identity(info);}
// Pin each ordinary directory against replacement while enumerating its children.
// Never follow junctions, symlinks or hard links; deletion is by the verified handle.
inline void erase(const std::filesystem::path& path,unsigned depth=0,bool tree=true,const QString& expected={}) {
    if(depth>16)fail();
    Handle item;
    for(unsigned attempt=0;;++attempt){item.value=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(item.value!=INVALID_HANDLE_VALUE||attempt==20||GetLastError()!=ERROR_SHARING_VIOLATION)break;Sleep(25);}
    if(item.value==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND)return;fail();}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(item.value,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))fail();
    if(!expected.isEmpty()&&identity(info)!=expected)fail();
    if((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!tree)fail();
    if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){for(const auto& child:std::filesystem::directory_iterator(path))erase(child.path(),depth+1);}
    else if(info.nNumberOfLinks!=1)fail();
    FILE_DISPOSITION_INFO disposition{TRUE};if(!SetFileInformationByHandle(item.value,FileDispositionInfo,&disposition,sizeof(disposition)))fail();
}
inline QString publish(const QString& file,const QString& folder) {
    Handle item{CreateFileW(std::filesystem::path(file.toStdWString()).c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if(item.value==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(item.value,&info) || info.nNumberOfLinks!=1 || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))fail();
    const QFileInfo original(file);
    for(int suffix=0;suffix<1000;++suffix){
        const auto filename=suffix==0?original.fileName():original.completeBaseName()+QString(" (%1)").arg(suffix)+(original.suffix().isEmpty()?QString():"."+original.suffix());
        const auto target=QDir(folder).filePath(filename);const auto name=std::filesystem::path(target.toStdWString()).make_preferred().wstring();
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO)+name.size()*sizeof(wchar_t),0);auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));memcpy(rename->FileName,name.data(),rename->FileNameLength);
        if(SetFileInformationByHandle(item.value,FileRenameInfo,rename,static_cast<DWORD>(buffer.size())))return target;
        if(GetLastError()!=ERROR_ALREADY_EXISTS&&GetLastError()!=ERROR_FILE_EXISTS)throw std::runtime_error("The final video cannot be saved. Check folder permissions and retry.");
    }
    throw std::runtime_error("Too many videos have this filename. Choose another folder.");
}
}
