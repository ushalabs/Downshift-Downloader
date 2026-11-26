#pragma once

#include <QString>

class QWidget;

namespace downshift::msix {
QString familyName();
QString stateDirectory();
QString mediaToolsDirectory();
bool finishFirstLaunchSetup();
bool chromeSetupAvailable();
QString mediaToolsInstaller();
QString systemPowerShell();
void showSetup(QWidget* parent);
}
