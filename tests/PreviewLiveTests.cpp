#include "gui/MainWindow.h"
#include "database/QueueStore.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QEvent>
#include <QLabel>
#include <QTableWidget>
#include <QJsonDocument>
#include <QThread>
#include <iostream>
class HiddenPreview final:public QObject{bool eventFilter(QObject* object,QEvent* event)override{if(event->type()==QEvent::Polish)if(auto* widget=qobject_cast<QWidget*>(object);widget&&widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
int main(int argc,char** argv){
    QApplication app(argc,argv);HiddenPreview hidden;app.installEventFilter(&hidden);if(argc!=4)return 2;QTemporaryDir temporary;
    try{
        const auto database=QDir(temporary.path()).filePath("queue.sqlite");downshift::QueueEntry entry;entry.id=QString(32,'a');entry.url=QString::fromLocal8Bit(argv[2]);entry.media=true;entry.mediaTitle="Selected video preview";entry.destination=QDir(temporary.path()).filePath("preview.mkv");entry.mediaFolder=temporary.path();entry.status=downshift::QueueStatus::Completed;entry.videoMetadata={{"originalPage",entry.url},{"thumbnail",QString::fromLocal8Bit(argv[1])}};
        {downshift::QueueStore store(database);store.save(entry.id,QJsonDocument(entry.json()).toJson(QJsonDocument::Compact));}
        downshift::QueueManager queue(database);downshift::MainWindow window(queue);window.show();auto* table=window.findChild<QTableWidget*>("downloadTable");auto* thumbnail=window.findChild<QLabel*>("previewFallback");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool selected=false;
        while(std::chrono::steady_clock::now()<deadline){QApplication::processEvents();if(window.ready()&&!selected){table->selectRow(0);selected=true;}if(thumbnail->toolTip()=="Video preview"){window.grab().save(QString::fromLocal8Bit(argv[3]),"BMP");std::cout<<"Actual HTTPS video thumbnail fetched and decoded by native Qt preview"<<std::endl;return 0;}QThread::msleep(10);}throw std::runtime_error("Actual metadata thumbnail did not load");
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
