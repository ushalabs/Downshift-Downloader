#pragma once
#include "core/DownloadQueue/QueueManager.h"
#include <QDialog>
#include <QProcess>
#include <QTimer>
#include <optional>
class QLabel;
class QComboBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
namespace downshift {
// Site extraction and stream merging are delegated to pinned, local media helpers.
// QProcess is asynchronous: no network or file work blocks the desktop event loop.
class MediaDialog final : public QDialog {
public:
    MediaDialog(QueueManager& queue,QString url,QString title,QWidget* parent=nullptr,QString folder={},QJsonObject context={},std::optional<QueueEntry> retained={});
    ~MediaDialog() override;
    void inspect();
    void stop() { if(!persisting_)cancel(); }
    void pause();
    void resume();
    bool canPause() const {return !metadata_&&!verifying_&&!transferFinished_&&!paused_&&!pauseRequested_&&!persisting_&&!historyId_.isEmpty()&&process_.state()==QProcess::Running;}
    bool paused() const {return paused_;}
    void present(){closeAfter_=false;showNormal();raise();activateWindow();}
    bool active() const {return persisting_||(!paused_&&!historyId_.isEmpty())||process_.state()!=QProcess::NotRunning;}
    void setDownloadName(const QString& name);
    void refreshBrowserSource(const QJsonObject& context);
    void reject() override {if(active()){closeAfter_=true;hide();}else QDialog::reject();}
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void inspectSource(bool browserSource);
    void launch(QStringList arguments,bool metadata);
    void readOutput();
    void finished(int code,QProcess::ExitStatus status);
    void download();
    void cancel();
    void settle(QueueStatus status,QString error = {});
    void retryable(QString error);
    void verify();
    void restorePaused(const QueueEntry& entry);
    void pausedResult();
    QStringList transferArguments() const;
    QString sourceUrl() const;
    bool browserFormats() const;
    QByteArray browserInfo() const;
    QueueManager& queue_;
    QStringList baseArguments() const;
    QString url_,tools_,lastError_,folder_,historyId_,savedFile_;
    QueueSettings settings_;
    QJsonObject metadataInfo_,context_;
    QProcess process_;
    QTimer deadline_;
    QByteArray output_,errors_,helperInput_;
    bool metadata_=true,cancelled_=false,closeAfter_=false,ready_=false,verifying_=false,persisting_=false;
    bool paused_=false,pauseRequested_=false,transferFinished_=false;
    bool browserSource_=false;
    double elapsed_=0;
    std::optional<QueueEntry> retained_;
    std::chrono::steady_clock::time_point started_,lastProgress_;
    void* job_=nullptr;
    QLabel *status_=nullptr,*rate_=nullptr;
    QComboBox* quality_=nullptr;
    QLineEdit *destination_=nullptr,*title_=nullptr;
    QProgressBar* progress_=nullptr;
    QPushButton *download_=nullptr,*cancel_=nullptr,*browse_=nullptr,*openFolder_=nullptr,*pause_=nullptr;
};
}
