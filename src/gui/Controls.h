#pragma once
#include <QComboBox>
#include <QListView>
#include <QStyledItemDelegate>
#include <QFrame>
namespace downshift {
inline void modernCombo(QComboBox* combo) {
    combo->setView(new QListView(combo));
    combo->setItemDelegate(new QStyledItemDelegate(combo));
    combo->view()->setFrameShape(QFrame::NoFrame);
    if(auto* frame=qobject_cast<QFrame*>(combo->view()->parentWidget())){frame->setFrameShape(QFrame::NoFrame);frame->setLineWidth(0);frame->setWindowFlag(Qt::FramelessWindowHint);frame->setBackgroundRole(QPalette::Base);frame->setAutoFillBackground(true);}
}
}
