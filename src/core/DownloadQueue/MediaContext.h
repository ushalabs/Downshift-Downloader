#pragma once
#include "BrowserVideo.h"
#include <QJsonObject>
#include <QUrl>
#include <algorithm>
namespace downshift {
inline bool validMediaContext(const QJsonObject& context) {
    for(auto i=context.begin();i!=context.end();++i){
        if(i.key()=="youtube"){if(!i.value().isObject()||!validBrowserVideo(i.value().toObject()))return false;continue;}
        if(i.key()=="detectedStream"){if(!i.value().isBool())return false;continue;}
        if(i.key()!="referer" && i.key()!="origin" && i.key()!="userAgent")return false;
        if(!i.value().isString())return false;const auto text=i.value().toString();
        if(text.size()>(i.key()=="referer"?8192:2048) || std::any_of(text.begin(),text.end(),[](QChar c){return c.unicode()<32||c.unicode()==127;}))return false;
        if(i.key()!="userAgent"){
            const QUrl header(text,QUrl::StrictMode);
            if(!header.isValid() || (header.scheme()!="https" && header.scheme()!="http") || header.host().isEmpty() || !header.userInfo().isEmpty())return false;
            if(i.key()=="origin" && ((!header.path().isEmpty() && header.path()!="/") || header.hasQuery() || header.hasFragment()))return false;
        }
    }
    return true;
}
}
