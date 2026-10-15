#include "MainWindow.h"
#include "Design.h"
#include "core/DownloadQueue/Categories.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHostAddress>
#include <QBuffer>
#include <QCache>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QUrl>
#include <QFileInfo>
#include <future>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
namespace downshift {
namespace {
bool thumbnailUrl(const QUrl& url){
    if(!url.isValid()||url.scheme()!="https"||url.host().isEmpty()||!url.userInfo().isEmpty()||url.toString().size()>4096)return false;
    if(url.host().compare("localhost",Qt::CaseInsensitive)==0||url.host().endsWith(".local",Qt::CaseInsensitive))return false;
    QHostAddress address(url.host());if(!address.isNull()){
        if(address.protocol()==QAbstractSocket::IPv6Protocol&&(address.toIPv6Address()[0]&0xfe)==0xfc)return false;
        const auto v4=address.toIPv4Address();if(address.protocol()==QAbstractSocket::IPv4Protocol&&(v4==0||(v4>>28)>=14||(v4>>22)==0x191))return false;if(address.isLoopback()||address.isLinkLocal()||address.isMulticast()||address==QHostAddress::Any||address==QHostAddress::AnyIPv6||(v4&&((v4>>24)==10||(v4>>24)==127||(v4>>16)==0xa9fe||(v4>>16)==0xc0a8||(v4>>20)==0xac1)))return false;
    }return true;
}
QPixmap rounded(const QImage& image){
    QPixmap result(128*2,72*2);result.setDevicePixelRatio(2);result.fill(Qt::transparent);QPainter painter(&result);painter.setRenderHint(QPainter::Antialiasing);QPainterPath clip;clip.addRoundedRect(QRectF(0,0,128,72),9,9);painter.setClipPath(clip);const auto scaled=image.scaled(256,144,Qt::KeepAspectRatio,Qt::SmoothTransformation);painter.fillRect(QRect(0,0,128,72),QColor("#202636"));painter.drawImage(QRectF((128-scaled.width()/2.)/2,(72-scaled.height()/2.)/2,scaled.width()/2.,scaled.height()/2.),scaled);return result;
}
}
void MainWindow::preview(const QueueEntry* entry){
    const bool video=entry&&(entry->media||downloadCategory(QFileInfo(entry->destination).fileName())=="Videos");thumbnail_->setVisible(video&&detailToggle_->isChecked());videoInfo_->setVisible(video&&detailToggle_->isChecked());
    if(entry){
        const auto original=entry->videoMetadata.value("originalPage").toString(entry->url);QUrl page(original);const bool open=page.isValid()&&(page.scheme()=="https"||page.scheme()=="http")&&!page.host().isEmpty()&&page.userInfo().isEmpty();
        const auto display=open?page.adjusted(QUrl::RemoveQuery|QUrl::RemoveFragment|QUrl::RemoveUserInfo).toDisplayString():QString("Original page unavailable");sourceLink_->setText(sourceLink_->fontMetrics().elidedText(display,Qt::ElideRight,qMax(100,sourceLink_->width()-24)));sourceLink_->setToolTip(display);sourceLink_->setProperty("originalPage",open?page:QUrl());sourceLink_->setEnabled(open);
        QStringList metadata;if(!page.host().isEmpty())metadata<<page.host();const auto duration=entry->videoMetadata.value("duration").toDouble();if(duration>0)metadata<<QString("%1:%2").arg(static_cast<int>(duration)/60).arg(static_cast<int>(duration)%60,2,10,QChar('0'));const auto height=entry->videoMetadata.value("height").toInt();if(height>0)metadata<<QString::number(height)+"p";if(entry->status==QueueStatus::Completed)metadata<<QFileInfo(entry->destination).suffix().toUpper();videoInfo_->setText(metadata.join(" · "));
    }
    const auto url=video?QUrl(entry->videoMetadata.value("thumbnail").toString()):QUrl();const auto key=video?entry->id+"/"+url.toString()+"/"+QString::number(detailToggle_->isChecked()):QString();
    if(key==previewKey_)return;previewKey_=key;if(thumbnailReply_){thumbnailReply_->abort();thumbnailReply_=nullptr;}
    thumbnail_->setPixmap(lineIcon(Symbol::Video,QColor("#EC1680")).pixmap(38,38));thumbnail_->setToolTip("Thumbnail unavailable");
    if(!video||!detailToggle_->isChecked())return;
    if(!thumbnailUrl(url)){localVideoPreview(*entry);return;}
    static QCache<QString,QPixmap> cache(8192);if(auto* image=cache.object(url.toString())){thumbnail_->setPixmap(*image);thumbnail_->setToolTip("Video preview");return;}
    QNetworkRequest request(url);request.setTransferTimeout(7000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);request.setRawHeader("Accept","image/*");
    auto* reply=thumbnailReply_=thumbnails_->get(request);reply->setReadBufferSize(2*1024*1024+1);auto bytes=std::make_shared<QByteArray>();
    connect(reply,&QNetworkReply::readyRead,this,[reply,bytes]{bytes->append(reply->readAll());if(bytes->size()>2*1024*1024)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,key,url]{
        if(thumbnailReply_==reply)thumbnailReply_=nullptr;reply->deleteLater();if(key!=previewKey_)return;
        if(reply->error()!=QNetworkReply::NoError){if(const auto* e=selected())localVideoPreview(*e);return;}
        bytes->append(reply->readAll());if(bytes->isEmpty()||bytes->size()>2*1024*1024||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200||!reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith("image/")){if(const auto* e=selected())localVideoPreview(*e);return;}
        auto decoded=std::make_shared<std::future<QImage>>(std::async(std::launch::async,[bytes]{QBuffer buffer(bytes.get());buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);const auto size=reader.size();if(!size.isValid()||size.width()>4096||size.height()>4096||static_cast<qint64>(size.width())*size.height()>4*1024*1024)return QImage();reader.setAutoTransform(true);reader.setScaledSize(size.scaled(512,288,Qt::KeepAspectRatio));return reader.read();}));
        auto* timer=new QTimer(this);timer->setInterval(30);connect(timer,&QTimer::timeout,this,[this,timer,decoded,key,url]{if(decoded->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();const auto image=decoded->get();if(image.isNull()){if(key==previewKey_)if(const auto* e=selected())localVideoPreview(*e);return;}const auto picture=rounded(image);cache.insert(url.toString(),new QPixmap(picture),qMax(1,picture.width()*picture.height()*picture.depth()/8/1024));if(key==previewKey_){thumbnail_->setPixmap(picture);thumbnail_->setToolTip("Video preview");}});timer->start();
    });
}
void MainWindow::localVideoPreview(const QueueEntry& entry){
    if(entry.status!=QueueStatus::Completed||!detailToggle_->isChecked())return;
    static QCache<QString,QPixmap> frames(4096);if(auto* frame=frames.object(entry.id)){thumbnail_->setPixmap(*frame);thumbnail_->setToolTip("Local video preview");return;}
    const auto key=previewKey_,id=entry.id;auto* process=new QProcess(this);process->setObjectName("localVideoPreview");
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=0x08000000;});
    process->setProgram(QDir(QCoreApplication::applicationDirPath()).filePath("media-tools/ffmpeg.exe"));process->setArguments({"-nostdin","-hide_banner","-loglevel","error","-protocol_whitelist","file,pipe","-i",entry.destination,"-frames:v","1","-vf","scale=256:144:force_original_aspect_ratio=decrease","-f","image2pipe","-vcodec","png","pipe:1"});
    auto bytes=std::make_shared<QByteArray>();auto* deadline=new QTimer(process);deadline->setSingleShot(true);connect(deadline,&QTimer::timeout,process,&QProcess::kill);connect(this,&QObject::destroyed,process,&QProcess::kill);
    connect(process,&QProcess::readyReadStandardError,process,[process]{process->readAllStandardError();});connect(process,&QProcess::readyReadStandardOutput,process,[process,bytes]{bytes->append(process->readAllStandardOutput());if(bytes->size()>2*1024*1024)process->kill();});
    connect(process,&QProcess::errorOccurred,process,[process](QProcess::ProcessError error){if(error==QProcess::FailedToStart)process->deleteLater();});
    connect(process,&QProcess::finished,this,[this,process,bytes,key,id](int code,QProcess::ExitStatus state){process->deleteLater();bytes->append(process->readAllStandardOutput());if(code||state!=QProcess::NormalExit||bytes->size()>2*1024*1024)return;const auto image=QImage::fromData(*bytes,"PNG");if(image.isNull())return;const auto picture=rounded(image);frames.insert(id,new QPixmap(picture),144);if(previewKey_==key){thumbnail_->setPixmap(picture);thumbnail_->setToolTip("Local video preview");}});process->start();deadline->start(10000);
}
}
