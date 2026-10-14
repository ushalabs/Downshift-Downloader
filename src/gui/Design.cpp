#include "Design.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QEvent>
#include <QWidget>
#include <QMap>
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
namespace downshift {
QIcon lineIcon(Symbol symbol,QColor color) {
    static QMap<QString,QIcon> cache;const auto key=QString::number(static_cast<int>(symbol))+"/"+color.name(QColor::HexArgb);if(cache.contains(key))return cache.value(key);
    QIcon icon;
    for(int size:{16,20,24,32,48,64}){
        QPixmap pixels(size,size);pixels.fill(Qt::transparent);QPainter p(&pixels);p.setRenderHint(QPainter::Antialiasing);p.scale(size/24.,size/24.);
        p.setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
        const auto line=[&](double a,double b,double c,double d){p.drawLine(QPointF(a,b),QPointF(c,d));};
        switch(symbol){
        case Symbol::Download:line(12,3,12,15);line(7,10,12,15);line(12,15,17,10);p.drawRoundedRect(QRectF(4,16,16,5),1,1);break;
        case Symbol::Activity:p.drawEllipse(QRectF(3,3,18,18));line(12,6,12,12);line(12,12,16,14);break;
        case Symbol::Check:p.drawEllipse(QRectF(3,3,18,18));line(7,12,10,15);line(10,15,17,8);break;
        case Symbol::Attention:{QPolygonF shape;shape<<QPointF(12,3)<<QPointF(22,21)<<QPointF(2,21);p.drawPolygon(shape);line(12,9,12,14);p.drawPoint(QPointF(12,18));break;}
        case Symbol::Settings:p.drawEllipse(QRectF(5,5,14,14));p.drawEllipse(QRectF(9,9,6,6));for(int i=0;i<8;++i){p.save();p.translate(12,12);p.rotate(i*45);line(0,-7,0,-10);p.restore();}break;
        case Symbol::Video:p.drawRoundedRect(QRectF(3,5,18,14),2,2);p.drawPolyline(QPolygonF{QPointF(10,9),QPointF(15,12),QPointF(10,15),QPointF(10,9)});break;
        case Symbol::Plus:line(12,4,12,20);line(4,12,20,12);break;
        case Symbol::Search:p.drawEllipse(QRectF(3,3,12,12));line(14,14,21,21);break;
        case Symbol::More:p.setPen(Qt::NoPen);p.setBrush(color);for(int x:{5,12,19})p.drawEllipse(QPointF(x,12),1.5,1.5);break;
        case Symbol::Folder:p.drawPolyline(QPolygonF{QPointF(3,19),QPointF(3,5),QPointF(9,5),QPointF(12,8),QPointF(21,8),QPointF(21,19),QPointF(3,19)});break;
        case Symbol::Chevron:line(7,9,12,14);line(12,14,17,9);break;
        case Symbol::Resume:p.drawPolygon(QPolygonF{QPointF(7,4),QPointF(20,12),QPointF(7,20)});break;
        case Symbol::Pause:line(8,5,8,19);line(16,5,16,19);break;
        case Symbol::Cancel:line(6,6,18,18);line(6,18,18,6);break;
        case Symbol::Audio:line(10,6,10,17);line(10,6,19,4);line(19,4,19,15);p.drawEllipse(QRectF(4,15,6,5));p.drawEllipse(QRectF(13,13,6,5));break;
        case Symbol::Program:p.drawRoundedRect(QRectF(3,4,18,16),2,2);line(3,9,21,9);p.drawPoint(QPointF(7,6.5));p.drawPoint(QPointF(10,6.5));break;
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
            DwmSetWindowAttribute(handle,20,&dark,sizeof(dark));const COLORREF caption=dark ? RGB(27,31,40):RGB(248,249,252);DwmSetWindowAttribute(handle,35,&caption,sizeof(caption));
        }return false;
    }
};
}
void applyDesign(bool dark) {
    static const bool watched=[] {auto* appearance=new WindowAppearance;appearance->setParent(qApp);qApp->installEventFilter(appearance);return true;}();(void)watched;
    qApp->setProperty("downshiftDark",dark);
    const QString bg=dark ? "#171b23":"#f8f9fc",surface=dark ? "#222733":"#ffffff",text=dark ? "#eef1f7":"#1b2030",muted=dark ? "#a8b1c2":"#697386",border=dark ? "#343c4d":"#e8eaf0",selection=dark ? "#492437":"#fce7f1",disabled=dark ? "#667083":"#a6adbb";
    QPalette palette;palette.setColor(QPalette::Window,QColor(bg));palette.setColor(QPalette::Base,QColor(surface));palette.setColor(QPalette::Button,QColor(surface));palette.setColor(QPalette::WindowText,QColor(text));palette.setColor(QPalette::Text,QColor(text));palette.setColor(QPalette::ButtonText,QColor(text));palette.setColor(QPalette::Highlight,QColor(selection));palette.setColor(QPalette::HighlightedText,QColor(text));palette.setColor(QPalette::Disabled,QPalette::Text,QColor(disabled));palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(disabled));qApp->setPalette(palette);
    qApp->setStyleSheet(QString(
        "QWidget{font-family:'Segoe UI';font-size:13px;color:%3;} QMainWindow,QDialog{background:%1;} QLabel{background:transparent;}"
        "QWidget#sidebar,QWidget#summaryCard,QWidget#detailsPanel,QWidget#emptyPanel{background:%2;border:1px solid %5;border-radius:12px;} QWidget#sidebar{border-radius:0;border-width:0 1px 0 0;}"
        "QLabel#brand{font-size:20px;font-weight:700;} QLabel#heading{font-size:26px;font-weight:700;} QLabel#sectionHeading,QLabel#detailTitle{font-size:17px;font-weight:600;} QLabel#muted{color:%4;} QLabel[metric=true]{font-size:23px;font-weight:700;} QLabel#emptyState{font-size:19px;font-weight:600;}"
        "QPushButton{background:%2;border:1px solid %5;border-radius:9px;padding:8px 12px;min-height:20px;font-weight:600;} QPushButton:hover{background:%6;border-color:#e91e79;} QPushButton:pressed{background:%6;} QPushButton:focus{border:1px solid #e91e79;}"
        "QPushButton[primary=true],QPushButton:default{background:#e91e79;color:white;border-color:#e91e79;} QPushButton[primary=true]:hover,QPushButton:default:hover{background:#d81b70;} QPushButton[primary=true]:pressed{background:#be185d;}"
        "QPushButton:disabled{color:%7;border-color:%5;background:%1;} QPushButton[primary=true]:disabled{background:%5;color:%7;} QPushButton[tertiary=true]{background:transparent;border-color:transparent;} QPushButton[destructive=true]{color:#dc4554;} QPushButton[destructive=true]:disabled{color:%7;}"
        "QPushButton#navigation{text-align:left;background:transparent;border:0;padding:10px 12px;} QPushButton#navigation:hover{background:%1;} QPushButton#navigation:checked{background:%6;color:#e91e79;} QWidget#sidebar[compact=true] QPushButton#navigation{text-align:center;}"
        "QLineEdit,QComboBox,QSpinBox{background:%2;border:1px solid %5;border-radius:9px;padding:8px 10px;min-height:20px;selection-background-color:#e91e79;selection-color:white;} QLineEdit:focus,QComboBox:focus,QSpinBox:focus{border-color:#e91e79;}"
        "QComboBox{padding-right:28px;} QComboBox::drop-down{border:0;width:26px;background:transparent;} QComboBox::down-arrow{image:url(:/branding/chevron.xpm);width:14px;height:14px;}"
        "QComboBox QAbstractItemView{background:%2;color:%3;border:1px solid %5;border-radius:9px;padding:5px;selection-background-color:%6;selection-color:%3;outline:0;} QComboBox QAbstractItemView::item{min-height:24px;padding:7px 10px;border:0;border-radius:6px;margin:1px;}"
        "QMenu{background:%2;color:%3;border:1px solid %5;border-radius:9px;padding:5px;} QMenu::item{padding:9px 24px;border-radius:6px;margin:1px;} QMenu::item:selected{background:%6;} QMenu::item:disabled{color:%7;} QMenu::separator{height:1px;background:%5;margin:4px 8px;}"
        "QTableWidget{background:%2;border:1px solid %5;border-radius:12px;selection-background-color:%6;selection-color:%3;outline:0;} QTableWidget::item{border-bottom:1px solid %5;} QHeaderView::section{background:%2;color:%4;border:0;border-bottom:1px solid %5;padding:12px 8px;font-size:12px;font-weight:600;}"
        "QProgressBar{background:%5;border:0;border-radius:5px;text-align:center;font-size:11px;} QProgressBar::chunk{background:#e91e79;border-radius:5px;} QScrollArea,QStackedWidget{background:transparent;border:0;}"
        "QScrollBar:vertical{background:transparent;width:8px;margin:2px;} QScrollBar::handle:vertical{background:%5;border-radius:3px;min-height:24px;} QScrollBar::handle:vertical:hover{background:%4;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        "QScrollBar:horizontal{background:transparent;height:8px;} QScrollBar::handle:horizontal{background:%5;border-radius:3px;min-width:24px;} QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
        "QListWidget#settingsSections{background:%2;border:0;outline:0;} QListWidget#settingsSections::item{padding:12px;border-radius:8px;margin:2px;} QListWidget#settingsSections::item:selected{background:%6;color:#e91e79;}"
        "QLabel#banner{background:%6;padding:10px;border-radius:8px;} QLabel#banner[error=true]{color:#dc4554;} QStatusBar{background:%1;color:%4;border-top:1px solid %5;font-size:11px;} QToolTip{background:%2;color:%3;border:1px solid %5;padding:5px;} QCheckBox{spacing:8px;} QDialog QLabel{background:transparent;}"
        ).arg(bg,surface,text,muted,border,selection,disabled));
    for(auto* widget:QApplication::topLevelWidgets())if(widget->isVisible()){const BOOL value=dark;const auto handle=reinterpret_cast<HWND>(widget->winId());DwmSetWindowAttribute(handle,20,&value,sizeof(value));const COLORREF caption=dark ? RGB(27,31,40):RGB(248,249,252);DwmSetWindowAttribute(handle,35,&caption,sizeof(caption));}
}
}
