#pragma once
#include <QString>
namespace downshift {
// Optional app-local binding makes normal Desktop and browser launches use one
// explicitly chosen per-user store. Smoke/tests never call this for their queues.
QString storageDirectory(const QString& applicationDirectory,const QString& userSid);
QString storageIdentity(const QString& file);
}
