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
DesignTokens DesignTokens::theme(bool dark) {
    if(dark)return {QColor("#171B27"),QColor("#252B3B"),QColor("#1D2230"),QColor("#202636"),QColor("#F4F6FC"),QColor("#A2ADC2"),QColor("#343D50"),QColor("#49253D"),QColor("#FF3B98"),QColor("#4AD6AB"),QColor("#E9B45C"),QColor("#FF7D8A")};
    return {QColor("#FFFFFF"),QColor("#FFFFFF"),QColor("#FAFBFD"),QColor("#FFFFFF"),QColor("#172033"),QColor("#64748B"),QColor("#DCE1EC"),QColor("#FBE4EF"),QColor("#EC1680"),QColor("#139B73"),QColor("#D58B18"),QColor("#DC4056")};
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
    const auto tokens=DesignTokens::theme(dark);
    const QString bg=tokens.background.name(),surface=tokens.surface.name(),text=tokens.text.name(),muted=tokens.muted.name(),border=tokens.border.name(),selection=tokens.selection.name(),disabled=dark?"#69768C":"#98A3B6";
    QPalette palette;palette.setColor(QPalette::Window,QColor(bg));palette.setColor(QPalette::Base,QColor(surface));palette.setColor(QPalette::Button,QColor(surface));palette.setColor(QPalette::WindowText,QColor(text));palette.setColor(QPalette::Text,QColor(text));palette.setColor(QPalette::ButtonText,QColor(text));palette.setColor(QPalette::Highlight,QColor(selection));palette.setColor(QPalette::HighlightedText,QColor(text));palette.setColor(QPalette::Disabled,QPalette::Text,QColor(disabled));palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(disabled));qApp->setPalette(palette);
    qApp->setStyleSheet(QString(
        "QWidget{font-family:'Segoe UI';font-size:13px;color:%3;} QMainWindow,QDialog{background:%1;} QLabel{background:transparent;}"
        "QWidget#sidebar,QWidget#summaryCard,QWidget#detailsPanel,QWidget#emptyPanel{background:%2;border:1px solid %5;border-radius:12px;} QWidget#sidebar{background:%8;border-radius:0;border-width:0 1px 0 0;}"
        "QLabel#brand{font-size:20px;font-weight:700;} QLabel#heading{font-size:28px;font-weight:700;} QLabel#sectionHeading,QLabel#detailTitle{font-size:17px;font-weight:600;} QLabel#muted{color:%4;} QLabel[metric=true]{font-size:22px;font-weight:700;} QLabel#emptyState{font-size:19px;font-weight:600;}"
        "QPushButton{background:%2;border:1px solid %5;border-radius:9px;padding:8px 12px;min-height:20px;font-weight:600;} QPushButton:hover{background:%6;border-color:#EC1680;} QPushButton:pressed{background:%6;} QPushButton:focus{border:1px solid #EC1680;}"
        "QPushButton[primary=true],QPushButton:default{background:#EC1680;color:white;border-color:#EC1680;} QPushButton[primary=true]:hover,QPushButton:default:hover{background:#D90C70;} QPushButton[primary=true]:pressed{background:#BA0B62;}"
        "QPushButton:disabled{color:%7;border-color:%5;background:%1;} QPushButton[primary=true]:disabled{background:%5;color:%7;} QPushButton[tertiary=true]{background:transparent;border-color:transparent;} QPushButton[destructive=true]{color:#dc4554;} QPushButton[destructive=true]:disabled{color:%7;}"
        "QPushButton#navigation{text-align:left;background:transparent;border:0;padding:10px 40px 10px 10px;} QLabel#navigationCount{background:%1;color:%4;border-radius:11px;font-size:11px;} QPushButton#navigation:checked QLabel#navigationCount{background:#EC1680;color:white;} QPushButton#navigation:hover{background:%1;} QPushButton#navigation:checked{background:%6;color:#EC1680;} QWidget#sidebar[compact=true] QPushButton#navigation{text-align:center;padding:10px 8px;}"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:%9;border:1px solid %5;border-radius:9px;padding:8px 10px;min-height:20px;selection-background-color:#EC1680;selection-color:white;} QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus{border-color:#EC1680;}"
        "QComboBox{combobox-popup:0;padding-right:28px;} QComboBox::drop-down{border:0;width:26px;background:transparent;} QComboBox::down-arrow{image:url(:/branding/chevron.xpm);width:14px;height:14px;}"
        "QFrame#choicePopup{background:%2;border:1px solid %5;border-radius:9px;} QComboBox QAbstractItemView{background:%2;color:%3;border:0;border-radius:0;padding:0;selection-background-color:%6;selection-color:%3;outline:0;} QComboBox QAbstractItemView::item{min-height:24px;padding:7px 10px;border:0;border-radius:6px;margin:1px;}"
        "QMenu{background:%2;color:%3;border:1px solid %5;border-radius:9px;padding:5px;} QMenu::item{padding:9px 24px;border-radius:6px;margin:1px;} QMenu::item:selected{background:%6;} QMenu::item:disabled{color:%7;} QMenu::separator{height:1px;background:%5;margin:4px 8px;}"
        "QTableWidget{background:%2;border:1px solid %5;border-radius:12px;selection-background-color:%6;selection-color:%3;outline:0;} QTableWidget::item{border-bottom:1px solid %5;} QHeaderView::section{background:%2;color:%4;border:0;border-bottom:1px solid %5;padding:12px 8px;font-size:12px;font-weight:600;}"
        "QProgressBar{background:%5;border:0;border-radius:5px;text-align:center;font-size:11px;} QProgressBar::chunk{background:#EC1680;border-radius:5px;} QScrollArea,QStackedWidget{background:transparent;border:0;}"
        "QScrollBar:vertical{background:transparent;width:8px;margin:2px;} QScrollBar::handle:vertical{background:%5;border-radius:3px;min-height:24px;} QScrollBar::handle:vertical:hover{background:%4;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        "QScrollBar:horizontal{background:transparent;height:8px;} QScrollBar::handle:horizontal{background:%5;border-radius:3px;min-width:24px;} QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
        "QListWidget#settingsSections{background:%2;border:0;outline:0;} QListWidget#settingsSections::item{padding:12px;border-radius:8px;margin:2px;} QListWidget#settingsSections::item:selected{background:%6;color:#EC1680;}"
        "QLabel#banner{background:%2;border:1px solid %5;padding:12px 36px 12px 14px;border-radius:10px;} QLabel#previewFallback{background:%6;border-radius:10px;color:%4;} QPushButton#sourceLink{text-align:left;border:0;background:transparent;color:#EC1680;padding:0;min-height:16px;font-weight:400;} QTabWidget::pane{border:0;border-top:1px solid %5;background:transparent;} QTabBar::tab{padding:7px 14px;color:%4;border-bottom:2px solid transparent;} QTabBar::tab:selected{color:#EC1680;border-bottom-color:#EC1680;} QLabel#banner[error=true]{color:#dc4554;} QStatusBar{background:%1;color:%4;border-top:1px solid %5;font-size:11px;} QToolTip{background:%2;color:%3;border:1px solid %5;padding:5px;} QCheckBox{spacing:8px;} QDialog QLabel{background:transparent;}"
        ).arg(bg,surface,text,muted,border,selection,disabled,tokens.sidebar.name(),tokens.input.name()).replace("#EC1680",tokens.pink.name()));
    for(auto* widget:QApplication::topLevelWidgets())if(widget->isVisible()){const BOOL value=dark;const auto handle=reinterpret_cast<HWND>(widget->winId());DwmSetWindowAttribute(handle,20,&value,sizeof(value));const COLORREF caption=dark ? RGB(27,31,40):RGB(248,249,252);DwmSetWindowAttribute(handle,35,&caption,sizeof(caption));}
}
}
