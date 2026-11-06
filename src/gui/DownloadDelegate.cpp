#include "DownloadDelegate.h"
#include "Design.h"
#include "core/DownloadQueue/QueueManager.h"
#include <QApplication>
#include <QPainter>
#include <algorithm>

namespace downshift {
namespace {
constexpr int cellPadding=8;

QColor statusColor(QueueStatus status,const DesignTokens& tokens) {
    switch(status){
    case QueueStatus::Running:return tokens.pink;
    case QueueStatus::Completed:return tokens.success;
    case QueueStatus::Failed:return tokens.error;
    case QueueStatus::Paused:return tokens.warning;
    default:return tokens.muted;
    }
}
}

int DownloadDelegate::statusColumnWidth(const QFont& baseFont) {
    QFont font=baseFont;font.setPixelSize(12);font.setWeight(QFont::Normal);
    const QFontMetrics metrics(font);
    // Reserve room for transient torrent states too, without resizing live rows.
    return qMax(112,qMax(metrics.horizontalAdvance("Connecting to peers"),metrics.horizontalAdvance("Waiting to download"))+2*cellPadding+12);
}

void DownloadDelegate::paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const {
    const auto tokens=DesignTokens::theme(qApp->property("downshiftDark").toBool());
    const auto rect=option.rect;
    const auto area=rect.adjusted(cellPadding,0,-cellPadding,0);
    const auto column=index.column();
    const bool selected=option.state.testFlag(QStyle::State_Selected);
    const bool hovered=option.state.testFlag(QStyle::State_MouseOver);
    p->save();p->setClipRect(rect,Qt::IntersectClip);p->setRenderHint(QPainter::Antialiasing);
    const bool dark=qApp->property("downshiftDark").toBool();
    p->fillRect(rect,selected?tokens.selection:hovered?QColor(dark?"#22232B":"#F5F5F7"):tokens.surface);
    QFont font=option.font;font.setPixelSize(12);font.setWeight(QFont::Normal);
    p->setFont(font);p->setPen(tokens.text);

    if(column==0){
        if(selected)p->fillRect(QRect(rect.left(),rect.top()+4,2,rect.height()-8),tokens.pink);
        const auto icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(p,QRect(area.left(),rect.center().y()-8,16,16));
        const auto left=area.left()+24,width=qMax(0,area.width()-24);
        const int top=rect.center().y()-16;
        font.setPixelSize(13);font.setWeight(QFont::Medium);p->setFont(font);
        p->drawText(QRect(left,top,width,17),Qt::AlignVCenter,p->fontMetrics().elidedText(index.data().toString(),Qt::ElideMiddle,width));
        font.setPixelSize(11);font.setWeight(QFont::Normal);p->setFont(font);p->setPen(tokens.muted);
        p->drawText(QRect(left,top+17,width,15),Qt::AlignVCenter,p->fontMetrics().elidedText(index.data(Qt::UserRole+1).toString(),Qt::ElideRight,width));
    }else if(column==3){
        const auto value=std::clamp(index.data(Qt::UserRole+1).toDouble(),0.,100.);
        const bool unknown=index.data(Qt::UserRole+2).toBool();
        font.setFeature(QFont::Tag("tnum"),1);p->setFont(font);
        if(!unknown){
            const auto status=static_cast<QueueStatus>(index.data(Qt::UserRole+3).toInt());
            const auto width=qMax(0,area.width()-p->fontMetrics().horizontalAdvance("100%")-8);
            const QRectF track(area.left(),rect.center().y()-2,width,4);
            p->setPen(Qt::NoPen);p->setBrush(tokens.border);p->drawRoundedRect(track,2,2);
            if(value>0){
                p->setBrush(statusColor(status,tokens));
                p->drawRoundedRect(QRectF(track.left(),track.top(),track.width()*value/100.,track.height()),2,2);
            }
        }
        p->setPen(unknown?tokens.muted:tokens.text);
        p->drawText(area,Qt::AlignRight|Qt::AlignVCenter,unknown?QString("—"):QString::number(value,'f',0)+"%");
    }else if(column==7){
        const auto status=static_cast<QueueStatus>(index.data(Qt::UserRole+1).toInt());
        p->setBrush(statusColor(status,tokens));p->setPen(Qt::NoPen);
        p->drawEllipse(QRectF(area.left(),rect.center().y()-2.5,6,6));
        const auto label=area.adjusted(12,0,0,0);p->setPen(tokens.text);
        p->drawText(label,Qt::AlignVCenter,p->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,qMax(0,label.width())));
    }else if(column==10){
        lineIcon(Symbol::More,tokens.muted).paint(p,QRect(rect.center().x()-8,rect.center().y()-8,16,16));
    }else{
        const auto text=index.data().toString();
        const bool numeric=column==1||column==2||column==4||column==5||column==6;
        if(numeric){font.setFeature(QFont::Tag("tnum"),1);p->setFont(font);}
        if(text=="—"||!numeric)p->setPen(tokens.muted);
        p->drawText(area,(numeric?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter,
                    p->fontMetrics().elidedText(text,Qt::ElideRight,qMax(0,area.width())));
    }
    p->setPen(selected?tokens.selection:tokens.border);p->drawLine(rect.bottomLeft(),rect.bottomRight());
    p->restore();
}
}
