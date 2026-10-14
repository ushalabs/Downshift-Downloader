#pragma once
#include <QColor>
#include <QIcon>
namespace downshift {
enum class Symbol {Download,Activity,Check,Attention,Settings,Video,Plus,Search,More,File,Folder,Chevron,Resume,Pause,Cancel,Archive,Audio,Program,Copy,Info};
QIcon lineIcon(Symbol symbol,QColor color=QColor("#697386"));
void applyDesign(bool dark);
}
