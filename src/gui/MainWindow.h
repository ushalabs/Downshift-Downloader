#pragma once
#include "core/DownloadQueue/QueueManager.h"
#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <optional>
class QTableWidget;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSystemTrayIcon;
namespace downshift {
class MainWindow final:public QMainWindow {
public:
    explicit MainWindow(QueueManager& queue,QWidget* parent=nullptr);
    bool ready() const { return ready_; }
    const QueueSnapshot& current() const { return current_; }
    void openAdd();
    void openSettings();
    void refresh();
private:
    template<class T,class F> void watch(std::future<T> future,F success) {
        auto pending=std::make_shared<std::future<T>>(std::move(future)); auto* timer=new QTimer(this); timer->setInterval(30);
        connect(timer,&QTimer::timeout,this,[this,timer,pending,success=std::move(success)]() mutable {
            if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
            timer->stop(); timer->deleteLater();
            try { if constexpr(std::is_void_v<T>) { pending->get(); success(); } else success(pending->get()); refresh(); }
            catch(const std::exception& e) { message(QString::fromUtf8(e.what()),true); }
        }); timer->start();
    }
    void apply(const QueueSnapshot& snapshot);
    void theme(bool dark);
    void filter();
    void details();
    void perform(QueueAction action);
    void more();
    void message(const QString& text,bool error=false);
    const QueueEntry* selected() const;
    QueueManager& queue_;
    QueueSnapshot current_;
    std::optional<std::future<QueueSnapshot>> pending_;
    bool ready_=false;
    QTableWidget* table_=nullptr;
    QLineEdit* search_=nullptr;
    QComboBox* filter_=nullptr;
    QLabel *summary_=nullptr,*speed_=nullptr,*banner_=nullptr,*empty_=nullptr,*detailTitle_=nullptr;
    std::vector<std::pair<QString,QLabel*>> detailLabels_;
    QPushButton *start_=nullptr,*pause_=nullptr,*cancel_=nullptr,*more_=nullptr;
    QSystemTrayIcon* tray_=nullptr;
};
QPushButton* makeButton(const QString& text,const char* name,QWidget* parent=nullptr);
}
