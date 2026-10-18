#pragma once
#include <QString>
#include <QRegularExpression>
namespace downshift {
inline bool validDownloadName(const QString& name) {
    if(name.isEmpty()||name.size()>150||name.endsWith('.')||name.endsWith(' '))return false;
    for(const auto c:name)if(c.unicode()<32||QString("<>:\"/\\|?*").contains(c))return false;
    const auto base=name.section('.',0,0).toUpper();
    return !QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])$").match(base).hasMatch();
}
inline QString suggestedDownloadName(QString name) {
    for(auto& c:name)if(c.unicode()<32||QString("<>:\"/\\|?*").contains(c))c='_';
    name=name.left(120);while(name.endsWith('.')||name.endsWith(' '))name.chop(1);
    return validDownloadName(name)?name:QString("Video");
}
}
