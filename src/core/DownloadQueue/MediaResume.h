#pragma once
#include <QJsonObject>
namespace downshift {
inline bool validMediaResume(const QJsonObject& recipe) {
    if(recipe.size()!=3||recipe.value("version").toInt()!=1||!recipe.value("height").isDouble()||!recipe.value("filename").isString())return false;
    const auto height=recipe.value("height").toDouble();if(height<0||height>16384||height!=recipe.value("height").toInt())return false;
    const auto name=recipe.value("filename").toString();if(name.size()>150||name.endsWith('.')||name.endsWith(' '))return false;
    for(const auto c:name)if(c.unicode()<32||QString("<>:\"/\\|?*").contains(c))return false;
    return name!="."&&name!="..";
}
}
