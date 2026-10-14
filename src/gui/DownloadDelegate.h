#pragma once
#include <QStyledItemDelegate>
namespace downshift {
class DownloadDelegate final:public QStyledItemDelegate {
public:
    explicit DownloadDelegate(QObject* parent):QStyledItemDelegate(parent){}
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override;
};
}
