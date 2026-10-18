#include "gui/MainWindow.h"
#include "gui/Controls.h"
#include "gui/Design.h"
#include "core/Runtime.h"
#include "database/QueueStore.h"
#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSslSocket>
#include <QStatusBar>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <cstring>
using namespace downshift;
namespace {
int checks=0;
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);++checks;}
template<class F>void wait(F condition,int ms=15000){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);while(std::chrono::steady_clock::now()<end){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(condition())return;QThread::msleep(10);}throw std::runtime_error("GUI overhaul wait timed out");}
template<class T>T* find(QWidget& widget,const char* name){auto* value=widget.findChild<T*>(name);if(!value)throw std::runtime_error(name);return value;}
class Hidden final:public QObject {bool eventFilter(QObject* object,QEvent* event)override{if(event->type()==QEvent::Polish)if(auto* widget=qobject_cast<QWidget*>(object);widget&&widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
class SourceReceiver final:public QObject {
    Q_OBJECT
public:QUrl opened;
public slots:void open(const QUrl& url){opened=url;}
};
class Reply final:public QNetworkReply {
    QByteArray bytes_;qint64 offset_=0;
public:
    Reply(const QNetworkRequest& request,QByteArray bytes,QObject* parent):QNetworkReply(parent),bytes_(std::move(bytes)){
        setRequest(request);setUrl(request.url());setOperation(QNetworkAccessManager::GetOperation);setHeader(QNetworkRequest::ContentTypeHeader,QString("image/jpeg"));setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        QTimer::singleShot(120,this,[this]{if(isFinished())return;emit readyRead();setFinished(true);emit finished();});
    }
    void abort()override{if(!isFinished()){setError(OperationCanceledError,"Cancelled");setFinished(true);emit finished();}}
    qint64 bytesAvailable()const override{return bytes_.size()-offset_+QNetworkReply::bytesAvailable();}
protected:qint64 readData(char* data,qint64 length)override{const auto n=qMin(length,bytes_.size()-offset_);if(n<=0)return -1;memcpy(data,bytes_.constData()+offset_,static_cast<std::size_t>(n));offset_+=n;return n;}
};
class Network final:public QNetworkAccessManager {
public:int requests=0;QByteArray jpeg;
protected:QNetworkReply* createRequest(Operation,const QNetworkRequest& request,QIODevice*)override{++requests;return new Reply(request,request.url().path().contains("bad")?QByteArray("invalid image"):jpeg,this);}
};
void select(QTableWidget* table,const QString& id){for(int n=0;n<table->rowCount();++n)if(table->item(n,0)->data(Qt::UserRole).toString()==id){table->selectRow(n);return;}throw std::runtime_error("Missing fixture row");}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);Hidden hidden;app.installEventFilter(&hidden);QCoreApplication::setApplicationName("Downshift");
    try{
        check(argc==2,"Screenshot folder supplied");const QString shots=QString::fromLocal8Bit(argv[1]);QDir().mkpath(shots);QTemporaryDir temporary;check(temporary.isValid(),"Private GUI queue");CurlRuntime curl;
        const auto database=QDir(temporary.path()).filePath("queue.sqlite");QString video,missing,bad;
        {QueueStore store(database);for(int n=0;n<200;++n){QueueEntry row;row.id=QString::number(n+1,16).rightJustified(32,'0');row.created=1792050000+n;row.destination=QDir(temporary.path()).filePath(QString("Document %1 with a long readable filename.txt").arg(n));row.url="https://files.example.org/document";row.status=n%3==0?QueueStatus::Paused:n%3==1?QueueStatus::Completed:QueueStatus::Failed;row.progress.totalBytes=805306368;row.progress.downloadedBytes=row.status==QueueStatus::Completed?805306368:0;
            if(n<3){row.media=true;row.status=QueueStatus::Completed;row.mediaTitle="Recorded video · source-linked preview";row.url="https://video.example.org/watch/123?private_token=hidden";row.mediaFolder=temporary.path();row.videoMetadata={{"originalPage",row.url},{"duration",866},{"height",1080}};if(n!=1)row.videoMetadata["thumbnail"]=QString("https://images.example.org/%1.jpg").arg(n==2?"bad":"video");if(n==0)video=row.id;else if(n==1)missing=row.id;else bad=row.id;}
            store.save(row.id,QJsonDocument(row.json()).toJson(QJsonDocument::Compact));}}
        Network network;QImage fixture(320,180,QImage::Format_RGB32);fixture.fill(QColor("#CA1678"));QBuffer image(&network.jpeg);image.open(QIODevice::WriteOnly);check(fixture.save(&image,"WEBP"),"WebP plugin available");QByteArray png;QBuffer pngBuffer(&png);pngBuffer.open(QIODevice::WriteOnly);check(fixture.save(&pngBuffer,"PNG"),"PNG support available");check(QSslSocket::supportsSsl(),"Native TLS backend available");
        QueueManager queue(database);MainWindow window(queue,nullptr,&network);window.show();wait([&]{return window.ready();});auto* table=find<QTableWidget>(window,"downloadTable");check(table->rowCount()==200,"Hundreds of encrypted rows load");wait([&]{return table->verticalScrollBar()->mapTo(table,QPoint{}).y()>=table->viewport()->geometry().top();});check(table->verticalScrollBar()->maximum()>0,"Long list has a working row scrollbar");check(!find<QPushButton>(window,"videoButton")->isVisible(),"No redundant dashboard video button");check(!window.findChild<QStatusBar*>(),"No bottom status bar");check(!find<QLabel>(window,"summary")->isVisible(),"No redundant summary line");check(table->horizontalHeader()->defaultAlignment().testFlag(Qt::AlignLeft),"Table headings align left");
        SourceReceiver source;QDesktopServices::setUrlHandler("https",&source,"open");
        select(table,video);auto* thumbnail=find<QLabel>(window,"previewFallback");wait([&]{return thumbnail->toolTip()=="Video preview";});check(network.requests==1,"Metadata preview loads asynchronously");check(!find<QPushButton>(window,"sourceLink")->text().contains("private_token"),"Signed query is absent from source display");check(find<QPushButton>(window,"sourceLink")->property("originalPage").toUrl().path()=="/watch/123","Original page remains distinct from thumbnail URL");find<QPushButton>(window,"sourceLink")->click();check(source.opened.path()=="/watch/123"&&source.opened.host()=="video.example.org","Source click opens the selected original page");QDesktopServices::unsetUrlHandler("https");
        select(table,missing);check(thumbnail->toolTip()=="Thumbnail unavailable"&&network.requests==1,"Old records use fallback without decorative requests");select(table,bad);wait([&]{return network.requests==2;});QThread::msleep(150);QApplication::processEvents();check(thumbnail->toolTip()=="Thumbnail unavailable","Malformed image does not affect task");select(table,video);wait([&]{return thumbnail->toolTip()=="Video preview";});check(network.requests==2,"Thumbnail cache avoids repeated requests");
        for(bool dark:{false,true}){applyDesign(dark);for(QSize size:{QSize(1075,730),QSize(1000,650),QSize(900,600),QSize(800,550)}){window.resize(size);wait([&]{return table->verticalScrollBar()->mapTo(table,QPoint{}).y()>=table->viewport()->geometry().top();});check(table->verticalScrollBar()->mapTo(table,QPoint{}).y()>=table->viewport()->geometry().top(),"Row scrollbar starts below fixed headings across themes and sizes");check(table->horizontalScrollBar()->maximum()==0,"Responsive table needs no horizontal scroll");auto* primary=find<QPushButton>(window,"addButton");check(window.rect().contains(primary->mapTo(&window,primary->rect().bottomRight())),"Primary action remains accessible");check(window.grab().save(QDir(shots).filePath(QString("%1-%2x%3.bmp").arg(dark?"dark":"light").arg(size.width()).arg(size.height())),"BMP"),"Theme and size screenshot exported");}}
        window.showMaximized();QApplication::processEvents();check(window.grab().save(QDir(shots).filePath("maximized.bmp"),"BMP"),"Maximized screenshot exported");window.showNormal();
        auto* toggle=find<QPushButton>(window,"detailsToggle");toggle->click();check(!thumbnail->isVisible(),"Collapsed details hide preview");wait([&]{return find<QWidget>(window,"detailsPanel")->height()<130;},3000);check(window.grab().save(QDir(shots).filePath("collapsed-details.bmp"),"BMP"),"Collapsed direction screenshot exported");toggle->click();check(thumbnail->isVisible(),"Expanded details restore preview");find<QPushButton>(window,"closeDetails")->click();check(!find<QWidget>(window,"detailsPanel")->isVisible(),"Details close button dismisses panel");select(table,video);
        applyDesign(false);window.openSettings();auto* settings=find<QDialog>(window,"settingsDialog");auto* signature=find<QLabel>(*settings,"settingsDeveloper");check(signature->font().italic()&&signature->text().contains("#111111")&&signature->text().contains("#ec1680"),"Settings developer signature uses italic black Usha and pink Labs");check(settings->grab().save(QDir(shots).filePath("settings-brand.bmp"),"BMP"),"Settings signature rendered");auto* sections=find<QListWidget>(*settings,"settingsSections");for(int n=0;n<sections->count();++n){sections->setCurrentRow(n);QApplication::processEvents();for(auto* combo:settings->findChildren<QComboBox*>())if(combo->isVisible()){combo->showPopup();QApplication::processEvents();auto* popup=combo->view()->window();check(popup->width()<=qMax(combo->width(),120)+2&&popup->height()<=8*36+14,"Dropdown dimensions are bounded");check(popup->screen()->availableGeometry().contains(popup->frameGeometry()),"Dropdown stays within screen");check(popup->windowFlags().testFlag(Qt::NoDropShadowWindowHint),"Native popup shadow disabled");check(!popup->mask().isEmpty()&&!popup->mask().contains(QPoint(0,0)),"Whole popup has rounded native bounds");check(popup->grab().save(QDir(shots).filePath(QString("popup-%1-%2.bmp").arg(n).arg(combo->objectName())),"BMP"),"Whole dropdown screenshot exported");combo->hidePopup();}}
        settings->reject();wait([&]{return !window.findChild<QDialog*>("settingsDialog");});
        Notification notice(window.centralWidget());for(const auto kind:{Notice::Success,Notice::Information,Notice::Warning}){notice.present("Routine notification",kind);wait([&]{return !notice.isVisible();},7500);++checks;}notice.present("Critical error",Notice::Critical);wait([&]{return notice.isVisible();});find<QPushButton>(notice,"dismissNotice")->click();check(!notice.isVisible(),"Critical error supports manual dismissal");
        auto* sort=find<QComboBox>(window,"viewDownloads");sort->setCurrentIndex(1);check(table->item(0,0)->data(Qt::UserRole).toString()==video,"Oldest ordering uses stored timestamps");find<QLineEdit>(window,"searchField")->setText("Video");int visible=0;for(int n=0;n<table->rowCount();++n)if(!table->isRowHidden(n))++visible;check(visible==3,"Type search matches video rows");
        std::cout<<"GUI overhaul checks passed: "<<checks<<"; scale="<<qEnvironmentVariable("QT_SCALE_FACTOR").toStdString()<<"; preview requests="<<network.requests<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<"GUI overhaul: "<<error.what()<<std::endl;return 1;}
}

#include "GuiOverhaulTests.moc"
