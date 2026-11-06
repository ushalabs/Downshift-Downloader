#pragma once

#include <QString>

class QWidget;

namespace downshift::msix {
QString familyName();
QString stateDirectory();
QString mediaToolsDirectory();
bool finishFirstLaunchSetup();
bool chromeSetupAvailable();
void showSetup(QWidget* parent);
}
