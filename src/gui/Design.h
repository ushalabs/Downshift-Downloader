#pragma once
#include <QColor>
#include <QIcon>
namespace downshift {
enum class Symbol {Download,Activity,Check,Attention,Settings,Video,Plus,Search,More,File,Folder,Chevron,Resume,Pause,Cancel,Archive,Audio,Program,Copy,Info,Gauge,Database,Torrent,Image,Link,ChevronUp,Apk};
struct DesignTokens {
    QColor background,surface,sidebar,input,text,muted,border,selection,pink,success,warning,error;
    static DesignTokens theme(bool dark);
    static constexpr int radius=6,rowHeight=44,controlHeight=32;
};
QIcon lineIcon(Symbol symbol,QColor color=QColor("#697386"));
QColor fileIconColor(Symbol symbol,bool dark);
void applyDesign(bool dark);
QString transferDuration(double seconds);
}
