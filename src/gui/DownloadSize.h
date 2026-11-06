#pragma once

#include <QLabel>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <limits>
#include <optional>

namespace downshift {
inline QString downloadSizeText(std::optional<quint64> size) {
    if(!size)return "Size unavailable";
    if(*size<1024)return QString::number(*size)+" B";
    return QLocale().formattedDataSize(static_cast<qint64>(*size),2,QLocale::DataSizeIecFormat);
}

// Inspect only the response headers. For a one-byte response, the size
// comes from the total in Content-Range rather than Content-Length.
class DownloadSizeProbe final : public QObject {
public:
    explicit DownloadSizeProbe(QLabel* label) : QObject(label), label_(label), network_(this) {
        debounce_.setSingleShot(true);
        debounce_.setInterval(300);
        connect(&debounce_,&QTimer::timeout,this,[this]{request(false);});
    }

    void inspect(const QUrl& url) {
        debounce_.stop();
        const auto old=reply_;
        reply_.clear();
        if(old)old->abort();
        url_=url;
        if(!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
            (url.scheme()!="http" && url.scheme()!="https")){
            label_->setText(downloadSizeText({}));
            return;
        }
        label_->setText("Checking size…");
        debounce_.start();
    }
private:
    void request(bool range) {
        QNetworkRequest query(url_);
        query.setTransferTimeout(5000);
        query.setMaximumRedirectsAllowed(5);
        query.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        query.setRawHeader("Accept-Encoding","identity");
        if(range)query.setRawHeader("Range","bytes=0-0");
        auto* reply=range?network_.get(query):network_.head(query);
        reply_=reply;
        reply->setReadBufferSize(4096);
        connect(reply,&QNetworkReply::finished,reply,&QObject::deleteLater);
        const auto complete=[this,reply,range] {
            if(reply_!=reply)return;
            const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(!reply->isFinished() && (!status || (status>=300 && status<400)))return;
            std::optional<quint64> total;
            const auto encoding=reply->rawHeader("Content-Encoding").trimmed().toLower();
            if(encoding.isEmpty() || encoding=="identity"){
                QByteArray length;
                if(status==200)length=reply->rawHeader("Content-Length").trimmed();
                else if(range && status==206){
                    static const QRegularExpression contentRange("^bytes 0-0/([1-9][0-9]*)$");
                    length=contentRange.match(QString::fromLatin1(reply->rawHeader("Content-Range").trimmed())).captured(1).toLatin1();
                }else if(range && status==416 && reply->rawHeader("Content-Range").trimmed()=="bytes */0")length="0";
                bool ok=false;
                const auto bytes=length.toULongLong(&ok);
                if(ok && bytes<=static_cast<quint64>(std::numeric_limits<qint64>::max()))total=bytes;
            }
            reply_.clear();
            if(!reply->isFinished())reply->abort();
            if(!total && !range && (status==200 || status==405 || status==501)){request(true);return;}
            label_->setText(downloadSizeText(total));
        };
        if(range)connect(reply,&QNetworkReply::metaDataChanged,this,complete);
        connect(reply,&QNetworkReply::finished,this,complete);
    }
    QLabel* label_;
    QNetworkAccessManager network_;
    QTimer debounce_;
    QPointer<QNetworkReply> reply_;
    QUrl url_;
};
}
