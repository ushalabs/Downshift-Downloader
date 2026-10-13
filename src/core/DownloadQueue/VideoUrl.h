#pragma once
#include <QRegularExpression>
#include <QUrl>
namespace downshift {
inline bool youtubeHost(const QString& host) {
    return host == "youtu.be" || host == "youtube.com" || host.endsWith(".youtube.com");
}
inline QString youtubeVideo(const QString& address) {
    const QUrl url(address);
    if (!youtubeHost(url.host()) || (url.scheme() != "http" && url.scheme() != "https") || !url.userInfo().isEmpty()) return {};
    QString id;
    if (url.host() == "youtu.be") id = url.path().section('/',1,1);
    else if (url.path() == "/watch") {
        for (const auto& field : url.query().split('&')) if (field.startsWith("v=")) id=field.mid(2);
    } else {
        const auto parts=url.path().split('/');
        if(parts.size()>=3 && (parts[1]=="shorts" || parts[1]=="embed" || parts[1]=="live")) id=parts[2];
    }
    return QRegularExpression("^[A-Za-z0-9_-]{11}$").match(id).hasMatch() ? "https://www.youtube.com/watch?v="+id : QString();
}
}
