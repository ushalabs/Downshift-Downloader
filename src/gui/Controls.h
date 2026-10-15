#pragma once
#include <QComboBox>
#include <QLabel>
#include <QTimer>
class QPushButton;
namespace downshift {
void modernCombo(QComboBox* combo);
enum class Notice { Success, Information, Warning, Critical };
class Notification final:public QLabel {
public:
    explicit Notification(QWidget* parent);
    void present(const QString& text,Notice kind);
    void position();
private:
    bool eventFilter(QObject*,QEvent*) override;
    QTimer timer_;
    QPushButton* close_;
    QString lastText_;
    Notice lastKind_=Notice::Information;
    bool dismissed_=false;
};
}
