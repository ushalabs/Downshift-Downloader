#include "DownloadDelegate.h"
#include "Design.h"
#include <QApplication>
#include <QPainter>
namespace downshift {
int DownloadDelegate::statusColumnWidth(const QFont& baseFont) {
    QFont font=baseFont;font.setPixelSize(11);font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    // Reserve the longest labels plus badge/cell padding and glyph rounding.
    // Keep the width stable as live torrent states change.
    return qMax(114,qMax(metrics.horizontalAdvance("Connecting to peers"),metrics.horizontalAdvance("Waiting to download"))+46);
}
void DownloadDelegate::paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const {
    if(index.column()==10){p->save();p->fillRect(option.rect,(option.state&QStyle::State_Selected)?option.palette.highlight():option.palette.base());lineIcon(Symbol::More,option.palette.text().color()).paint(p,option.rect.adjusted(8,20,-8,-20));p->restore();return;}
    if(index.column()!=0 && index.column()!=3 && index.column()!=7){auto clean=option;clean.state&=~QStyle::State_HasFocus;QStyledItemDelegate::paint(p,clean,index);return;}
    p->save();p->setRenderHint(QPainter::Antialiasing);const auto dark=qApp->property("downshiftDark").toBool();
    p->fillRect(option.rect,(option.state & QStyle::State_Selected) ? option.palette.highlight():option.state & QStyle::State_MouseOver ? QBrush(DesignTokens::theme(dark).input):option.palette.base());
    if(index.column()==0&&(option.state&QStyle::State_Selected))p->fillRect(QRect(option.rect.left(),option.rect.top(),2,option.rect.height()),DesignTokens::theme(dark).pink);
    const auto area=option.rect.adjusted(10,0,-10,0);p->setPen(option.palette.text().color());
    if(index.column()==0){
        const auto icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));icon.paint(p,QRect(area.left(),area.center().y()-16,32,32));
        const auto left=area.left()+44,width=qMax(0,area.width()-44);QFont font=option.font;font.setPixelSize(13);font.setWeight(QFont::DemiBold);p->setFont(font);
        p->drawText(QRect(left,area.top()+12,width,22),Qt::AlignVCenter,p->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,width));
        font.setPixelSize(11);font.setWeight(QFont::Normal);p->setFont(font);p->setPen(QColor(dark ? "#a8b1c2":"#697386"));
        p->drawText(QRect(left,area.top()+35,width,18),Qt::AlignVCenter,p->fontMetrics().elidedText(index.data(Qt::UserRole+1).toString(),Qt::ElideRight,width));
    }else if(index.column()==3){
        const auto value=index.data(Qt::UserRole+1).toDouble();const auto width=qMax(20,area.width()-38);const QRectF track(area.left(),area.center().y()-5,width,10);p->setPen(Qt::NoPen);p->setBrush(QColor(dark ? "#343c4d":"#e8eaf0"));p->drawRoundedRect(track,5,5);
        if(value>0){const auto status=index.data(Qt::UserRole+3).toInt();const auto tokens=DesignTokens::theme(dark);p->setBrush(status==3?tokens.success:status==4||status==5?tokens.error:status==2?tokens.muted:tokens.pink);p->drawRoundedRect(QRectF(track.left(),track.top(),track.width()*value/100.,track.height()),5,5);}p->setPen(option.palette.text().color());QFont font=option.font;font.setPixelSize(11);p->setFont(font);p->drawText(area,Qt::AlignRight|Qt::AlignVCenter,index.data(Qt::UserRole+2).toBool() ? QString("—"):QString::number(value,'f',0)+"%");
    }else{
        const auto status=index.data(Qt::UserRole+1).toInt();QColor color=status==3 ? QColor(dark ? "#48d79f":"#168256"):status==4 || status==5 ? QColor(dark ? "#ff7d8a":"#b83142"):status==1 ? QColor("#e91e79"):status==0 || status==6 ? QColor(dark ? "#81b7ff":"#2366ba"):QColor(dark ? "#e9bd76":"#916015");
        const auto text=index.data().toString();QFont font=option.font;font.setPixelSize(11);font.setWeight(QFont::DemiBold);p->setFont(font);const auto width=qMin(area.width(),p->fontMetrics().horizontalAdvance(text)+26);const QRectF badge(area.left(),area.center().y()-13,width,26);QColor tint=color;tint.setAlpha(dark ? 38:20);p->setPen(Qt::NoPen);p->setBrush(tint);p->drawRoundedRect(badge,6,6);p->setPen(color);const auto label=badge.adjusted(12,0,-12,0);p->drawText(label,Qt::AlignCenter,p->fontMetrics().elidedText(text,Qt::ElideRight,qMax(0,static_cast<int>(label.width()))));
    }
    p->setPen(QColor(dark ? "#343c4d":"#e8eaf0"));p->drawLine(option.rect.bottomLeft(),option.rect.bottomRight());p->restore();
}
}
