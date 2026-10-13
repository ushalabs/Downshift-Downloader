#pragma once
#include "core/DownloadQueue/QueueManager.h"
#include <QDialog>
#include <QProcess>
#include <QTimer>
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
    MediaDialog(QueueManager& queue,QString url,QString title,QWidget* parent=nullptr,QString folder={});
    ~MediaDialog() override;
    void inspect();
    void stop() { if(!persisting_)cancel(); }
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void launch(QStringList arguments,bool metadata);
    void readOutput();
    void finished(int code,QProcess::ExitStatus status);
    void download();
    void cancel();
    void settle(QueueStatus status,QString error = {});
    void retryable(QString error);
    void verify();
    QueueManager& queue_;
    QStringList baseArguments() const;
    QString url_,tools_,lastError_,folder_,historyId_,savedFile_;
    QueueSettings settings_;
    QProcess process_;
    QTimer deadline_;
    QByteArray output_,errors_;
    bool metadata_=true,cancelled_=false,closeAfter_=false,ready_=false,verifying_=false,persisting_=false;
    std::chrono::steady_clock::time_point started_,lastProgress_;
    void* job_=nullptr;
    QLabel *title_=nullptr,*status_=nullptr,*rate_=nullptr;
    QComboBox* quality_=nullptr;
    QLineEdit* destination_=nullptr;
    QProgressBar* progress_=nullptr;
    QPushButton *download_=nullptr,*cancel_=nullptr,*browse_=nullptr,*openFolder_=nullptr;
};
}
