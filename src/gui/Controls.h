#pragma once
#include <QComboBox>
#include <QListView>
#include <QStyledItemDelegate>
namespace downshift {
inline void modernCombo(QComboBox* combo) {
    combo->setView(new QListView(combo));
    combo->setItemDelegate(new QStyledItemDelegate(combo));
}
}
