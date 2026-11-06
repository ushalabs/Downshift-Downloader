#include "Design.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QEvent>
#include <QFontDatabase>
#include <QWidget>
#include <QMap>
#include <cmath>
#include <limits>
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
namespace downshift {
DesignTokens DesignTokens::theme(bool dark) {
    if(dark)return {QColor("#16171C"),QColor("#16171C"),QColor("#16171C"),QColor("#22232B"),QColor("#F3F3F3"),QColor("#ADAEB9"),QColor("#2D2E36"),QColor("#342533"),QColor("#FF66AE"),QColor("#6BC79F"),QColor("#DBB777"),QColor("#FF8A95")};
    return {QColor("#FFFFFF"),QColor("#FFFFFF"),QColor("#FFFFFF"),QColor("#FFFFFF"),QColor("#202020"),QColor("#616161"),QColor("#E3E3E8"),QColor("#F9E8F0"),QColor("#C7156C"),QColor("#237B55"),QColor("#98671B"),QColor("#B82F42")};
}
QString transferDuration(double seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds>=static_cast<double>(std::numeric_limits<qint64>::max()))return "—";
    const auto n=static_cast<qint64>(seconds);
    if(n>=3600)return QString("%1h %2m").arg(n/3600).arg((n/60)%60);
    if(n>=60)return QString("%1m %2s").arg(n/60).arg(n%60);
    return QString("%1s").arg(n);
}
QColor fileIconColor(Symbol symbol,bool dark) {
    switch(symbol){
    case Symbol::Audio:return QColor(dark?"#D0A0FA":"#8042AB");
    case Symbol::Archive:return QColor(dark?"#F5BC77":"#935009");
    case Symbol::Video:return QColor(dark?"#FF80BE":"#C7156C");
    case Symbol::Program:return QColor(dark?"#8BAEFF":"#3457AA");
    case Symbol::File:return QColor(dark?"#79C7E8":"#236D92");
    case Symbol::Apk:return QColor(dark?"#B4D88A":"#4C741B");
    case Symbol::Image:return QColor(dark?"#6AD8C5":"#007868");
    case Symbol::Torrent:return DesignTokens::theme(dark).pink;
    default:return DesignTokens::theme(dark).muted;
    }
}
QIcon lineIcon(Symbol symbol,QColor color) {
    static QMap<QString,QIcon> cache;const auto key=QString::number(static_cast<int>(symbol))+"/"+color.name(QColor::HexArgb);if(cache.contains(key))return cache.value(key);
    QIcon icon;
    for(int size:{16,20,24,32,48,64}){
        QPixmap pixels(size,size);pixels.fill(Qt::transparent);QPainter p(&pixels);p.setRenderHint(QPainter::Antialiasing);p.scale(size/24.,size/24.);
        p.setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
        const auto line=[&](double a,double b,double c,double d){p.drawLine(QPointF(a,b),QPointF(c,d));};
        switch(symbol){
        case Symbol::Download:line(12,3,12,15);line(7,10,12,15);line(12,15,17,10);p.drawRoundedRect(QRectF(4,16,16,5),1,1);break;
        case Symbol::Activity:p.drawPolyline(QPolygonF{QPointF(2,12),QPointF(7,12),QPointF(10,5),QPointF(14,20),QPointF(17,10),QPointF(22,10)});break;
        case Symbol::Gauge:p.drawArc(QRectF(3,4,18,18),0,180*16);line(5,14,4,18);line(19,14,20,18);line(12,15,17,8);p.drawEllipse(QRectF(10,13,4,4));break;
        case Symbol::Database:p.drawEllipse(QRectF(4,3,16,6));line(4,6,4,18);line(20,6,20,18);p.drawArc(QRectF(4,9,16,6),180*16,180*16);p.drawArc(QRectF(4,15,16,6),180*16,180*16);break;
        case Symbol::Torrent:p.drawArc(QRectF(4,5,16,17),180*16,180*16);line(4,13,4,4);line(20,13,20,4);line(4,8,8,8);line(16,8,20,8);break;
        case Symbol::Image:p.drawRoundedRect(QRectF(3,3,18,18),2,2);p.drawEllipse(QRectF(6,6,4,4));p.drawPolyline(QPolygonF{QPointF(4,18),QPointF(10,12),QPointF(14,16),QPointF(18,10),QPointF(21,14)});break;
        case Symbol::Link:p.drawArc(QRectF(2,8,13,9),45*16,270*16);p.drawArc(QRectF(10,5,12,9),225*16,270*16);line(8,14,16,10);break;
        case Symbol::Check:p.drawEllipse(QRectF(3,3,18,18));line(7,12,10,15);line(10,15,17,8);break;
        case Symbol::Attention:{QPolygonF shape;shape<<QPointF(12,3)<<QPointF(22,21)<<QPointF(2,21);p.drawPolygon(shape);line(12,9,12,14);p.drawPoint(QPointF(12,18));break;}
        case Symbol::Settings:p.drawEllipse(QRectF(5,5,14,14));p.drawEllipse(QRectF(9,9,6,6));for(int i=0;i<8;++i){p.save();p.translate(12,12);p.rotate(i*45);line(0,-7,0,-10);p.restore();}break;
        case Symbol::Video:p.drawRoundedRect(QRectF(3,5,18,14),2,2);p.drawPolyline(QPolygonF{QPointF(10,9),QPointF(15,12),QPointF(10,15),QPointF(10,9)});break;
        case Symbol::Plus:line(12,4,12,20);line(4,12,20,12);break;
        case Symbol::Search:p.drawEllipse(QRectF(3,3,12,12));line(14,14,21,21);break;
        case Symbol::More:p.setPen(Qt::NoPen);p.setBrush(color);for(int x:{5,12,19})p.drawEllipse(QPointF(x,12),1.5,1.5);break;
        case Symbol::Folder:p.drawPolyline(QPolygonF{QPointF(3,19),QPointF(3,5),QPointF(9,5),QPointF(12,8),QPointF(21,8),QPointF(21,19),QPointF(3,19)});break;
        case Symbol::ChevronUp:line(7,14,12,9);line(12,9,17,14);break;
        case Symbol::Chevron:line(7,9,12,14);line(12,14,17,9);break;
        case Symbol::Resume:p.drawPolygon(QPolygonF{QPointF(7,4),QPointF(20,12),QPointF(7,20)});break;
        case Symbol::Pause:line(8,5,8,19);line(16,5,16,19);break;
        case Symbol::Cancel:line(6,6,18,18);line(6,18,18,6);break;
        case Symbol::Audio:line(10,6,10,17);line(10,6,19,4);line(19,4,19,15);p.drawEllipse(QRectF(4,15,6,5));p.drawEllipse(QRectF(13,13,6,5));break;
        case Symbol::Program:p.drawRoundedRect(QRectF(3,4,18,16),2,2);line(3,9,21,9);p.drawPoint(QPointF(7,6.5));p.drawPoint(QPointF(10,6.5));break;
        case Symbol::Apk:p.drawRoundedRect(QRectF(6,7,12,12),2,2);line(8,7,6,3);line(16,7,18,3);line(3,10,3,17);line(21,10,21,17);line(9,19,9,22);line(15,19,15,22);p.drawPoint(QPointF(9,11));p.drawPoint(QPointF(15,11));break;
        case Symbol::Copy:p.drawRoundedRect(QRectF(8,8,13,13),2,2);p.drawPolyline(QPolygonF{QPointF(5,16),QPointF(3,16),QPointF(3,3),QPointF(16,3),QPointF(16,5)});break;
        case Symbol::Info:p.drawEllipse(QRectF(3,3,18,18));line(12,11,12,17);p.drawPoint(QPointF(12,7));break;
        default:p.drawPolyline(QPolygonF{QPointF(6,21),QPointF(6,3),QPointF(14,3),QPointF(19,8),QPointF(19,21),QPointF(6,21)});line(14,3,14,8);line(14,8,19,8);if(symbol==Symbol::Archive){line(11,9,11,18);line(11,14,14,14);}break;
        }
        p.end();icon.addPixmap(pixels);
    }cache.insert(key,icon);return icon;
}
namespace {
class WindowAppearance final:public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Show)if(auto* widget=qobject_cast<QWidget*>(object);widget && widget->isWindow() && widget->windowType()!=Qt::Popup){
            const BOOL dark=qApp->property("downshiftDark").toBool();const auto handle=reinterpret_cast<HWND>(widget->winId());
            DwmSetWindowAttribute(handle,20,&dark,sizeof(dark));const COLORREF caption=dark ? RGB(22,23,28):RGB(255,255,255);DwmSetWindowAttribute(handle,35,&caption,sizeof(caption));
        }return false;
    }
};
}
void applyDesign(bool dark) {
    static const bool watched=[] {auto* appearance=new WindowAppearance;appearance->setParent(qApp);qApp->installEventFilter(appearance);return true;}();(void)watched;
    qApp->setProperty("downshiftDark",dark);
    const auto tokens=DesignTokens::theme(dark);
    static const QString family=[] {
        const auto installed=QFontDatabase::families();
        for(const auto* name:{"Segoe UI Variable Text","Segoe UI Variable","Segoe UI"})
            if(installed.contains(QString::fromLatin1(name),Qt::CaseInsensitive))return QString::fromLatin1(name);
        return QApplication::font().family();
    }();
    auto font=QFont(family);font.setPixelSize(13);qApp->setFont(font);
    const QString bg=tokens.background.name(),surface=tokens.surface.name(),text=tokens.text.name(),muted=tokens.muted.name(),border=tokens.border.name(),selection=tokens.selection.name(),disabled=dark?"#858585":"#777777";
    QPalette palette;palette.setColor(QPalette::Window,QColor(bg));palette.setColor(QPalette::Base,QColor(surface));palette.setColor(QPalette::Button,QColor(surface));palette.setColor(QPalette::WindowText,QColor(text));palette.setColor(QPalette::Text,QColor(text));palette.setColor(QPalette::ButtonText,QColor(text));palette.setColor(QPalette::Highlight,QColor(selection));palette.setColor(QPalette::HighlightedText,QColor(text));palette.setColor(QPalette::Disabled,QPalette::Text,QColor(disabled));palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(disabled));qApp->setPalette(palette);
    qApp->setStyleSheet(QString(
        "QWidget{font-family:'@interfaceFont@';font-size:13px;color:%3;} QMainWindow,QDialog{background:%1;} QLabel{background:transparent;}"
        "QWidget#sidebar{background:%8;border-right:1px solid %5;} QWidget#navigationPanel{border:1px solid %5;border-radius:10px;} QWidget#detailsPanel,QWidget#emptyPanel{background:%2;border:1px solid %5;border-radius:6px;}"
        "QWidget#masthead,QWidget#commandBar{background:%2;border-bottom:1px solid %5;} QFrame#summarySeparator{background:%5;}"
        "QLabel#brand{font-size:19px;font-weight:600;} QLabel#heading{font-size:22px;font-weight:600;} QLabel#sectionHeading,QLabel#detailTitle{font-size:15px;font-weight:600;} QLabel#muted{color:%4;} QLabel[metric=true]{font-weight:600;} QLabel#emptyState{font-size:16px;font-weight:600;}"
        "QPushButton{background:%2;border:1px solid %5;border-radius:5px;padding:5px 10px;min-height:20px;} QPushButton:hover,QPushButton:pressed{background:%6;} QPushButton:focus{border-color:#EC1680;}"
        "QPushButton[primary=true],QPushButton:default{background:#C41467;color:white;border-color:#C41467;} QPushButton[primary=true]:hover,QPushButton:default:hover{background:#AD125A;} QPushButton[primary=true]:pressed{background:#96104D;} QPushButton[primary=true]:focus,QPushButton:default:focus{border-color:%3;}"
        "QPushButton:disabled{color:%7;border-color:%5;background:%1;} QPushButton[primary=true]:disabled{background:%5;color:%7;} QPushButton[tertiary=true]{background:transparent;border-color:transparent;} QPushButton[destructive=true]{color:@errorColor@;} QPushButton[destructive=true]:disabled{color:%7;}"
        "QWidget#commandBar QPushButton[transferCommand=true]{background:transparent;border:1px solid transparent;padding:0;min-height:46px;max-height:46px;}"
        "QWidget#commandBar QPushButton[transferCommand=true]:hover{background:@hoverColor@;} QWidget#commandBar QPushButton[transferCommand=true]:pressed{background:%6;} QWidget#commandBar QPushButton[transferCommand=true]:focus{border-color:#EC1680;}"
        "QPushButton#navigation,QPushButton[railCommand=true]{text-align:left;background:transparent;border:1px solid transparent;padding:5px 6px;} QPushButton#navigation{border-left:2px solid transparent;} QPushButton#navigation[navCount=true]{padding-right:32px;} QLabel#navigationCount{color:%4;font-size:11px;}"
        "QPushButton#navigation[categoryNav=true]{padding-top:3px;padding-bottom:3px;}"
        "QPushButton#categoryToggle{padding:0;} QPushButton#categoryToggle:hover,QPushButton#navigation:hover,QPushButton[railCommand=true]:hover{background:@hoverColor@;} QPushButton#navigation:checked{background:%6;border-left-color:#EC1680;} QPushButton#navigation:focus,QPushButton[railCommand=true]:focus{border-color:#EC1680;}"
        "QWidget#sidebar[compact=true] QPushButton#navigation,QWidget#sidebar[compact=true] QPushButton[railCommand=true]{text-align:center;padding:5px 4px;}"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:%9;border:1px solid %5;border-radius:5px;padding:5px 8px;min-height:20px;selection-background-color:#C41467;selection-color:white;} QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus{border-color:#EC1680;}"
        "QComboBox{combobox-popup:0;padding-right:26px;} QComboBox::drop-down{border:0;width:24px;background:transparent;} QComboBox::down-arrow{image:url(:/branding/chevron.xpm);width:12px;height:12px;}"
        "QFrame#choicePopup{background:%2;border:1px solid %5;border-radius:6px;} QComboBox QAbstractItemView{background:%2;color:%3;border:0;padding:0;selection-background-color:%6;selection-color:%3;outline:0;} QComboBox QAbstractItemView::item{padding:4px 8px;border:0;border-radius:4px;}"
        "QMenu{background:%2;color:%3;border:1px solid %5;border-radius:6px;padding:4px;} QMenu::item{padding:6px 20px;border-radius:4px;} QMenu::item:selected{background:%6;} QMenu::item:disabled{color:%7;} QMenu::separator{height:1px;background:%5;margin:4px 8px;}"
        "QTableWidget{background:%2;border:1px solid %5;border-radius:6px;selection-background-color:%6;selection-color:%3;outline:0;} QTableWidget#downloadTable{border:0;border-radius:0;} QTableWidget#downloadTable QHeaderView::section{background:%2;padding-top:8px;padding-bottom:8px;} QTableWidget::item{border-bottom:1px solid %5;} QHeaderView::section{background:%1;color:%4;border:0;border-bottom:1px solid %5;padding:6px 8px;font-size:12px;font-weight:600;}"
        "QProgressBar{background:%5;border:0;border-radius:3px;text-align:center;font-size:11px;} QProgressBar::chunk{background:#EC1680;border-radius:3px;} QScrollArea,QStackedWidget{background:transparent;border:0;}"
        "QScrollBar:vertical{background:transparent;width:14px;margin:0 2px;} QScrollBar:horizontal{background:transparent;height:14px;margin:2px 0;}"
        "QScrollBar::handle{background:@scrollThumb@;border-radius:5px;} QScrollBar::handle:hover,QScrollBar::handle:pressed{background:@scrollThumbActive@;}"
        "QScrollBar::handle:vertical{min-height:30px;} QScrollBar::handle:horizontal{min-width:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"
        "QListWidget#settingsSections{background:%2;border:0;outline:0;} QListWidget#settingsSections::item{padding:8px;border-radius:5px;margin:2px;} QListWidget#settingsSections::item:selected{background:%6;color:#EC1680;}"
        "QLabel#settingsDeveloper{color:@signatureColor@;}"
        "QLabel#banner{background:%2;border:1px solid %5;padding:8px 36px 8px 12px;border-radius:6px;} QLabel#previewFallback{background:%6;border-radius:6px;color:%4;} QPushButton#sourceLink{text-align:left;border:0;background:transparent;color:#EC1680;padding:0;min-height:16px;font-weight:400;} QTabWidget::pane{border:0;border-top:1px solid %5;background:transparent;} QTabBar::tab{padding:5px 10px;color:%4;border-bottom:2px solid transparent;} QTabBar::tab:selected{color:#EC1680;border-bottom-color:#EC1680;} QLabel#banner[error=true]{color:#dc4554;} QStatusBar{background:%1;color:%4;border-top:1px solid %5;font-size:11px;} QToolTip{background:%2;color:%3;border:1px solid %5;padding:5px;} QCheckBox{spacing:8px;} QDialog QLabel{background:transparent;}"
        ).arg(bg,surface,text,muted,border,selection,disabled,tokens.sidebar.name(),tokens.input.name())
         .replace("#EC1680",tokens.pink.name()).replace("@interfaceFont@",family).replace("@errorColor@",tokens.error.name())
         .replace("@hoverColor@",dark?"#22232B":"#F5F5F7")
         .replace("@signatureColor@",dark?"#FFFFFF":"#111111")
         .replace("@scrollThumb@",dark?"#656875":"#888C98").replace("@scrollThumbActive@",dark?"#9296A4":"#616674"));
    for(auto* widget:QApplication::topLevelWidgets())if(widget->isVisible()){const BOOL value=dark;const auto handle=reinterpret_cast<HWND>(widget->winId());DwmSetWindowAttribute(handle,20,&value,sizeof(value));const COLORREF caption=dark ? RGB(22,23,28):RGB(255,255,255);DwmSetWindowAttribute(handle,35,&caption,sizeof(caption));}
}
}
