#pragma once
#include <QFileInfo>
#include <QStringList>
namespace downshift {
enum class FileCategory { Audio, Archive, Video, Program, Document, Apk, Image };

inline FileCategory fileCategory(const QString& filename,const QString& mime={}) {
    const auto type=mime.section(';',0,0).trimmed().toLower();
    const auto ext=QFileInfo(filename).suffix().toLower();
    if(type.startsWith("video/"))return FileCategory::Video;
    if(type.startsWith("audio/"))return FileCategory::Audio;
    if(type.startsWith("image/"))return FileCategory::Image;

    static const QStringList images={"jpg","jpeg","png","gif","webp","avif","bmp","tif","tiff","ico","svg","heic","heif"};
    static const QStringList videos={"mp4","mkv","webm","avi","mov","wmv","flv","mpeg","mpg","m4v","ts","3gp"};
    static const QStringList audio={"mp3","m4a","aac","wav","flac","ogg","opus","wma","aiff"};
    static const QStringList documents={"pdf","doc","docx","docm","dot","dotx","ppt","pptx","pptm","pps","ppsx","pot","potx",
        "xls","xlsx","xlsm","xlsb","xlt","xltx","odt","ods","odp","rtf","txt","md","csv","tsv","epub"};
    static const QStringList archives={"zip","rar","7z","gz","bz2","xz","tar","tgz","zst"};
    if(ext=="apk"||type=="application/vnd.android.package-archive")return FileCategory::Apk;
    if(images.contains(ext))return FileCategory::Image;
    if(videos.contains(ext))return FileCategory::Video;
    if(audio.contains(ext))return FileCategory::Audio;
    if(documents.contains(ext))return FileCategory::Document;
    if(archives.contains(ext))return FileCategory::Archive;
    if(type=="application/pdf"||type=="application/msword"||type=="application/rtf"
        ||type=="text/rtf"||type=="text/plain"||type=="text/csv"||type=="text/markdown"||type=="text/tab-separated-values"
        ||type=="application/epub+zip"||type.startsWith("application/vnd.openxmlformats-officedocument.")
        ||type.startsWith("application/vnd.oasis.opendocument.")||type.startsWith("application/vnd.ms-word")
        ||type.startsWith("application/vnd.ms-excel")||type.startsWith("application/vnd.ms-powerpoint"))
        return FileCategory::Document;
    if(type.contains("zip")||type.contains("compressed")||type.contains("x-tar")||type.contains("x-rar"))
        return FileCategory::Archive;
    return FileCategory::Program;
}

inline QString downloadCategory(const QString& filename,const QString& mime={}) {
    switch(fileCategory(filename,mime)){
    case FileCategory::Audio:return "Audios";
    case FileCategory::Archive:return "Zip";
    case FileCategory::Video:return "Videos";
    case FileCategory::Document:return "Docs";
    case FileCategory::Image:return "Pictures";
    case FileCategory::Apk:
    case FileCategory::Program:return "Programs";
    }
    return "Programs";
}
}
