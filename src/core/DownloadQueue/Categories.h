#pragma once
#include <QFileInfo>
#include <QStringList>
namespace downshift {
inline QString downloadCategory(const QString& filename,const QString& mime={}) {
    const auto type=mime.toLower(),ext=QFileInfo(filename).suffix().toLower();
    if(type.startsWith("video/"))return "Videos";
    if(type.startsWith("audio/"))return "Audios";
    if(QStringList{"mp4","mkv","webm","avi","mov","wmv","flv","mpeg","mpg","m4v","ts","3gp"}.contains(ext))return "Videos";
    if(QStringList{"mp3","m4a","aac","wav","flac","ogg","opus","wma","aiff"}.contains(ext))return "Audios";
    if(QStringList{"zip","rar","7z","gz","bz2","xz","tar","tgz","zst"}.contains(ext) || type.contains("zip") || type.contains("compressed") || type.contains("x-tar") || type.contains("x-rar"))return "Zip";
    return "Programs";
}
}
