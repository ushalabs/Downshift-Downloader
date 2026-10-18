#pragma once
#include "core/DownloadQueue/QueueManager.h"
#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include "torrent/TorrentEngine.h"
#include <optional>
#include <map>
class QTableWidget;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSystemTrayIcon;
class QScrollArea;
class QTabWidget;
class QStackedWidget;
class QNetworkAccessManager;
class QNetworkReply;
namespace downshift {
class MainWindow final:public QMainWindow {
public:
    explicit MainWindow(QueueManager& queue,QWidget* parent=nullptr,QNetworkAccessManager* previewNetwork=nullptr);
    bool ready() const { return ready_; }
    const QueueSnapshot& current() const { return current_; }
    void openAdd(QString address={},QString filename={});
    void confirmDownload(const QueueEntry& entry);
    void captureError(){message("Connecting your browser download to Downshift…",false);}
    void openSettings();
    void openVideo(QString url,QString title,QString folder={},QJsonObject context={},QString filename={});
    void resumeVideo(const QueueEntry& entry);
    void showDownloads();
    void requestExit();
    void refresh();
    void attachTorrents(torrent::TorrentEngine& engine);
    void openTorrent(QString source={});
    void chromeStatus(QString status){chromeStatus_=std::move(status);if(auto* label=findChild<QLabel*>("chromeStatus"))label->setText("Chrome integration: "+chromeStatus_);}
private:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
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
    void preview(const QueueEntry* entry);
    void localVideoPreview(const QueueEntry& entry);
    void perform(QueueAction action);
    void more();
    void message(const QString& text,bool error=false);
    const QueueEntry* selected() const;
    const torrent::Snapshot* selectedTorrent() const;
    void torrentMenu();
    void torrentDetails(const QString& id);
    void renderTorrentRow(int row,const torrent::Snapshot& snapshot);
    void watchTorrent(std::future<void> future);
    QueueManager& queue_;
    QueueSnapshot current_;
    std::optional<std::future<QueueSnapshot>> pending_;
    bool ready_=false;
    QString chromeStatus_="Not connected";
    QTableWidget* table_=nullptr;
    QLineEdit* search_=nullptr;
    QComboBox* view_=nullptr;
    int page_=0;
    struct DisplayRow { bool torrent=false;std::size_t index=0; };
    std::vector<DisplayRow> displayOrder_;
    torrent::TorrentEngine* torrents_=nullptr;
    std::vector<torrent::Snapshot> torrentRows_;
    std::optional<std::future<std::vector<torrent::Snapshot>>> torrentPending_;
    bool torrentClosing_=false,torrentClosed_=false,exitRequested_=false;
    bool torrentBusy_=false;
    QLabel *summary_=nullptr,*speed_=nullptr,*banner_=nullptr,*empty_=nullptr,*detailTitle_=nullptr;
    QLabel *totalMetric_=nullptr,*activeMetric_=nullptr,*dataMetric_=nullptr,*emptyHelp_=nullptr,*identity_=nullptr;
    QWidget *sidebar_=nullptr,*emptyPanel_=nullptr,*detailsContainer_=nullptr;
    QScrollArea* detailScroll_=nullptr;
    QLabel *thumbnail_=nullptr,*videoInfo_=nullptr,*connectionInfo_=nullptr,*taskLog_=nullptr;
    QPushButton *sourceLink_=nullptr,*detailFolder_=nullptr,*detailCopy_=nullptr,*detailAction_=nullptr;
    QTabWidget* detailTabs_=nullptr;
    QStackedWidget* torrentDetailsStack_=nullptr;
    std::map<QString,QWidget*> torrentPanels_;
    QNetworkAccessManager* thumbnails_=nullptr;
    QString previewKey_;
    QNetworkReply* thumbnailReply_=nullptr;
    QPushButton *add_=nullptr,*video_=nullptr,*emptyAdd_=nullptr,*detailToggle_=nullptr,*retryHistory_=nullptr;
    std::vector<std::pair<QString,QLabel*>> detailLabels_;
    QPushButton *start_=nullptr,*pause_=nullptr,*cancel_=nullptr,*more_=nullptr;
    QSystemTrayIcon* tray_=nullptr;
};
QPushButton* makeButton(const QString& text,const char* name,QWidget* parent=nullptr);
}
