#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>
#include <algorithm>
namespace downshift {
inline bool browserVideoUrl(const QString& text,bool manifest=false) {
    if(text.size()>8192 || std::any_of(text.begin(),text.end(),[](QChar c){return c.unicode()<=32||c.unicode()==127;}))return false;
    const QUrl url(text,QUrl::StrictMode);
    // Like the existing media API, descriptors accept HTTP(S) sources without
    // credentials. The extension separately restricts YouTube observations to
    // Google's actual media hosts. No local-file or executable protocols enter.
    if(!url.isValid() || (url.scheme()!="https"&&url.scheme()!="http") || url.host().isEmpty() || !url.userInfo().isEmpty())return false;
    if(!(url.host()=="googlevideo.com"||url.host().endsWith(".googlevideo.com")))return true;
    if(manifest)return QRegularExpression("/api/manifest/(hls(_variant|_playlist)?|dash)/").match(url.path()).hasMatch();
    const QUrlQuery query(url);
    return url.path()=="/videoplayback" && !query.hasQueryItem("range") && query.queryItemValue("ump")!="1" && !query.hasQueryItem("sabr");
}
inline bool validBrowserVideo(const QJsonObject& info) {
    const QSet<QString> keys{"id","duration","formats","manifest"};for(auto it=info.begin();it!=info.end();++it)if(!keys.contains(it.key()))return false;
    if(!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(info.value("id").toString()).hasMatch() || !info.value("duration").isDouble())return false;
    const auto duration=info.value("duration").toDouble();if(!std::isfinite(duration)||duration<=0||duration>604800)return false;
    if(!info.value("formats").isArray() || info.value("formats").toArray().size()>12 || QJsonDocument(info).toJson(QJsonDocument::Compact).size()>48000)return false;
    if(info.contains("manifest") && (!info.value("manifest").isString()||!browserVideoUrl(info.value("manifest").toString(),true)))return false;
    bool video=false,audio=false;QSet<QString> ids;
    for(const auto& value:info.value("formats").toArray()){
        if(!value.isObject())return false;const auto f=value.toObject();
        const QSet<QString> fields{"url","format_id","ext","vcodec","acodec","filesize","height"};for(auto it=f.begin();it!=f.end();++it)if(!fields.contains(it.key()))return false;
        const auto id=f.value("format_id").toString(),ext=f.value("ext").toString(),v=f.value("vcodec").toString(),a=f.value("acodec").toString();
        if(!QRegularExpression("^[0-9]{1,5}$").match(id).hasMatch()||ids.contains(id)||!browserVideoUrl(f.value("url").toString())||!QStringList{"mp4","webm","m4a"}.contains(ext)||(v!="none"&&v!="unknown")||(a!="none"&&a!="unknown")||(v=="none"&&a=="none"))return false;
        ids.insert(id);const auto bytes=f.value("filesize").toDouble();if(!f.value("filesize").isDouble()||!std::isfinite(bytes)||bytes<=0||bytes>1e13||std::floor(bytes)!=bytes)return false;
        if(v!="none"){const auto height=f.value("height").toDouble();if(!f.value("height").isDouble()||height<1||height>16384||std::floor(height)!=height)return false;video=true;}
        if(a!="none")audio=true;
    }
    return info.contains("manifest") || (video&&audio);
}
}
