#include "Controls.h"
#include "Design.h"
#include <QApplication>
#include <QEvent>
#include <QListView>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QStyle>
#include <QPainterPath>
#include <QPainter>
#include <QRegion>
#include <QLayout>
namespace downshift {
namespace {
class ChoiceDelegate final:public QStyledItemDelegate {
public: using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override {auto size=QStyledItemDelegate::sizeHint(option,index);size.setHeight(36);return size;}
};
class PopupBounds final:public QObject {
    QComboBox* combo_;
    void roundAfterNativeEvent(){
        QTimer::singleShot(0,this,[this]{auto* popup=combo_->view()->window();if(auto* frame=qobject_cast<QFrame*>(popup))frame->setFrameShape(QFrame::NoFrame);QPainterPath shape;shape.addRoundedRect(QRectF(popup->rect()),9,9);popup->setMask(QRegion(shape.toFillPolygon().toPolygon()));});
    }
public:explicit PopupBounds(QComboBox* combo):QObject(combo),combo_(combo){}
    bool eventFilter(QObject* object,QEvent* event)override {
        if(event->type()==QEvent::Paint){
            // Qt's private combo container restores its native square frame on
            // style changes. Paint one rounded surface instead of that frame.
            auto* popup=qobject_cast<QWidget*>(object);const auto tokens=DesignTokens::theme(qApp->property("downshiftDark").toBool());QPainter painter(popup);painter.setCompositionMode(QPainter::CompositionMode_Source);painter.fillRect(popup->rect(),Qt::transparent);painter.setCompositionMode(QPainter::CompositionMode_SourceOver);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(tokens.border);painter.setBrush(tokens.surface);painter.drawRoundedRect(QRectF(popup->rect()).adjusted(.5,.5,-.5,-.5),9,9);return true;
        }
        if(event->type()==QEvent::Resize)roundAfterNativeEvent();
        if(event->type()==QEvent::Show){
            auto* popup=combo_->view()->window();if(auto* frame=qobject_cast<QFrame*>(popup))frame->setFrameShape(QFrame::NoFrame);if(popup->layout())popup->layout()->setContentsMargins(6,6,6,6);const auto origin=combo_->mapToGlobal(QPoint(0,combo_->height()+4));auto* screen=QGuiApplication::screenAt(origin);if(!screen)screen=combo_->screen();const auto available=screen->availableGeometry().adjusted(6,6,-6,-6);
            const int width=qMin(available.width(),qMax(combo_->width(),120));const int height=qMin(available.height(),qMin(combo_->count(),8)*36+12);
            const int x=qBound(available.left(),origin.x(),available.right()-width+1);int y=origin.y();if(y+height>available.bottom())y=combo_->mapToGlobal(QPoint(0,0)).y()-height-4;y=qBound(available.top(),y,available.bottom()-height+1);
            popup->setMinimumSize(0,0);popup->setMaximumSize(available.size());popup->setGeometry(x,y,width,height);combo_->view()->setMinimumWidth(0);roundAfterNativeEvent();
        }return false;
    }
};
}
void modernCombo(QComboBox* combo){
    if(combo->property("modernChoice").toBool())return;combo->setProperty("modernChoice",true);combo->setStyleSheet("QComboBox { combobox-popup:0; }");combo->setMaxVisibleItems(8);
    auto* view=new QListView(combo);view->setUniformItemSizes(true);view->setWordWrap(false);view->setTextElideMode(Qt::ElideRight);view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);view->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);view->setFrameShape(QFrame::NoFrame);
    combo->setView(view);combo->setItemDelegate(new ChoiceDelegate(combo));auto* popup=view->window();
    popup->setWindowFlags(Qt::Popup|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint);if(auto* frame=qobject_cast<QFrame*>(popup)){frame->setFrameShape(QFrame::NoFrame);frame->setLineWidth(0);}
    popup->setObjectName("choicePopup");popup->setAttribute(Qt::WA_TranslucentBackground);popup->setAutoFillBackground(false);if(popup->layout())popup->layout()->setContentsMargins(1,1,1,1);popup->installEventFilter(new PopupBounds(combo));
}
Notification::Notification(QWidget* parent):QLabel(parent),close_(new QPushButton(this)){
    setObjectName("banner");setTextFormat(Qt::PlainText);setWordWrap(true);hide();close_->setObjectName("dismissNotice");close_->setAccessibleName("Dismiss notification");close_->setToolTip("Dismiss");close_->setIcon(lineIcon(Symbol::Cancel));close_->setFixedSize(26,26);close_->setStyleSheet("padding:3px;border:0;background:transparent;");
    timer_.setSingleShot(true);connect(&timer_,&QTimer::timeout,this,&QWidget::hide);connect(close_,&QPushButton::clicked,this,[this]{dismissed_=true;hide();});parent->installEventFilter(this);
}
bool Notification::eventFilter(QObject*,QEvent* event){if(event->type()==QEvent::Resize)position();return false;}
void Notification::position(){const int width=qMin(540,qMax(100,parentWidget()->width()-32));setFixedWidth(width);adjustSize();move(parentWidget()->width()-width-16,parentWidget()->height()-height()-16);close_->move(width-31,8);raise();}
void Notification::present(const QString& text,Notice kind){
    if(kind==Notice::Critical&&dismissed_&&text==lastText_&&kind==lastKind_)return;
    lastText_=text;lastKind_=kind;dismissed_=false;
    timer_.stop();setText(text);setProperty("error",kind==Notice::Critical);setProperty("severity",static_cast<int>(kind));const auto tokens=DesignTokens::theme(qApp->property("downshiftDark").toBool());
    const auto color=kind==Notice::Success?tokens.success:kind==Notice::Warning?tokens.warning:kind==Notice::Critical?tokens.error:tokens.muted;
    setStyleSheet(QString("QLabel#banner{border-left:3px solid %1;}").arg(color.name()));style()->unpolish(this);style()->polish(this);position();show();raise();
    const int duration=kind==Notice::Success?3000:kind==Notice::Information?4000:kind==Notice::Warning?6000:0;if(duration)timer_.start(duration);
}
}
