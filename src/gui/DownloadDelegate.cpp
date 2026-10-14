#include "DownloadDelegate.h"
#include <QApplication>
#include <QPainter>
namespace downshift {
void DownloadDelegate::paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const {
    if(index.column()!=0 && index.column()!=3 && index.column()!=7){QStyledItemDelegate::paint(p,option,index);return;}
    p->save();p->setRenderHint(QPainter::Antialiasing);const auto dark=qApp->property("downshiftDark").toBool();
    p->fillRect(option.rect,(option.state & QStyle::State_Selected) ? option.palette.highlight():option.palette.base());
    const auto area=option.rect.adjusted(10,0,-10,0);p->setPen(option.palette.text().color());
    if(index.column()==0){
        const auto icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));icon.paint(p,QRect(area.left(),area.center().y()-16,32,32));
        const auto left=area.left()+44,width=qMax(0,area.width()-44);QFont font=option.font;font.setPixelSize(13);font.setWeight(QFont::DemiBold);p->setFont(font);
        p->drawText(QRect(left,area.top()+12,width,22),Qt::AlignVCenter,p->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,width));
        font.setPixelSize(11);font.setWeight(QFont::Normal);p->setFont(font);p->setPen(QColor(dark ? "#a8b1c2":"#697386"));
        p->drawText(QRect(left,area.top()+35,width,18),Qt::AlignVCenter,p->fontMetrics().elidedText(index.data(Qt::UserRole+1).toString(),Qt::ElideRight,width));
    }else if(index.column()==3){
        const auto value=index.data(Qt::UserRole+1).toDouble();const auto width=qMax(20,area.width()-38);const QRectF track(area.left(),area.center().y()-5,width,10);p->setPen(Qt::NoPen);p->setBrush(QColor(dark ? "#343c4d":"#e8eaf0"));p->drawRoundedRect(track,5,5);
        if(value>0){p->setBrush(QColor("#e91e79"));p->drawRoundedRect(QRectF(track.left(),track.top(),track.width()*value/100.,track.height()),5,5);}p->setPen(option.palette.text().color());QFont font=option.font;font.setPixelSize(11);p->setFont(font);p->drawText(area,Qt::AlignRight|Qt::AlignVCenter,index.data(Qt::UserRole+2).toBool() ? QString("—"):QString::number(value,'f',0)+"%");
    }else{
        const auto status=index.data(Qt::UserRole+1).toInt();QColor color=status==3 ? QColor(dark ? "#48d79f":"#168256"):status==4 || status==5 ? QColor(dark ? "#ff7d8a":"#b83142"):status==1 ? QColor("#e91e79"):status==0 || status==6 ? QColor(dark ? "#81b7ff":"#2366ba"):QColor(dark ? "#e9bd76":"#916015");
        const auto text=index.data().toString();QFont font=option.font;font.setPixelSize(11);font.setWeight(QFont::DemiBold);p->setFont(font);const auto width=qMin(area.width(),p->fontMetrics().horizontalAdvance(text)+24);const QRectF badge(area.left(),area.center().y()-13,width,26);QColor tint=color;tint.setAlpha(dark ? 38:20);p->setPen(Qt::NoPen);p->setBrush(tint);p->drawRoundedRect(badge,6,6);p->setPen(color);p->drawText(badge,Qt::AlignCenter,text);
    }
    p->setPen(QColor(dark ? "#343c4d":"#e8eaf0"));p->drawLine(option.rect.bottomLeft(),option.rect.bottomRight());p->restore();
}
}
