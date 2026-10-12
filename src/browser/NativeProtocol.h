#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace downshift::browser {
inline constexpr auto hostName="org.ushalabs.downshift";
inline constexpr auto extensionId="empnpdkfnffpcgnfgdndkbielplcoioe";
inline constexpr unsigned maximumMessage=65536;
QJsonObject parse(const QByteArray& data);
void validate(const QJsonObject& request);
QString pipeName();
QJsonObject send(const QJsonObject& request);
bool sameUserProcess(unsigned long process);
QString userSid();
}
