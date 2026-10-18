#include "gui/MainWindow.h"
#include "gui/Brand.h"
#include "core/Runtime.h"
#include "database/QueueStore.h"
#include "TestWindows.h"
#include <QScreen>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLineEdit>
#include <QJsonDocument>
#include <QAbstractItemView>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QListWidget>
#include <QScrollBar>
#include <QKeyEvent>
#include <QTableWidget>
#include <QThread>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message);++checks; }
template<class T> T* widget(QObject& parent,const char* name) { auto* result=parent.findChild<T*>(name); if(!result)throw std::runtime_error(std::string("Missing GUI control: ")+name);return result; }
template<class F> void wait(F condition,int timeout=15000) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(std::chrono::steady_clock::now()<end) {QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(condition())return;QThread::msleep(5);}
    throw std::runtime_error("GUI condition timed out");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv); QApplication::setApplicationName("Downshift");QApplication::setOrganizationName("Usha Labs");QApplication::setApplicationVersion("0.4.2");
    HiddenTestWindows hidden;app.installEventFilter(&hidden);std::cout<<"GUI device pixel ratio: "<<app.primaryScreen()->devicePixelRatio()<<'\n';
    if(argc!=5)return 2;const auto root=QString::fromLocal8Bit(argv[1]),url=QString::fromLatin1(argv[2]),sha=QString::fromLatin1(argv[3]),shots=QString::fromLocal8Bit(argv[4]);
    QDir().mkpath(shots);downshift::CurlRuntime curl;const auto database=QDir(root).filePath("queue.sqlite"),file=QDir(root).filePath("Verified download.bin");
    int beats=0;double maximumGap=0;bool measuring=false;auto last=std::chrono::steady_clock::now();QTimer heartbeat;heartbeat.setInterval(20);QObject::connect(&heartbeat,&QTimer::timeout,[&]{const auto now=std::chrono::steady_clock::now();if(measuring){maximumGap=std::max(maximumGap,std::chrono::duration<double>(now-last).count());++beats;}last=now;});heartbeat.start();
    try {
        check(!downshift::brandIcon().pixmap(32,32).isNull(),"Brand icon decodes with the shipped Qt image features");
        check(!QPixmap(":/branding/chevron.xpm").isNull(),"Quality selector arrow decodes with shipped Qt");
        {
            downshift::QueueManager queue(database);downshift::MainWindow window(queue);check(widget<QLabel>(window,"emptyState")->text().contains("Loading"),"Startup cannot falsely display empty history before loading");window.show();wait([&]{return window.ready();});check(!window.current().settings.dark,"Light must be default");
            check(window.width()<=1075 && window.height()<=730,"Comfortable default window");check(window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6),"Compact columns adapt");window.resize(1200,750);QApplication::processEvents();
            // A small Windows/CI display can clamp the requested native window size.
            std::cout<<"Responsive initial native size: "<<window.width()<<'x'<<window.height()<<"; connection column hidden="<<window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)<<'\n';
            wait([&]{return window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)==(window.width()<1180);},3000);
            std::cout<<"Responsive native size: "<<window.width()<<'x'<<window.height()<<"; connection column hidden="<<window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)<<'\n';
            check(window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)==(window.width()<1180),"Columns follow actual native window width");
            QResizeEvent expanded(QSize(1200,750),window.size());QApplication::sendEvent(&window,&expanded);check(!window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6),"Expanded resize policy shows connection details");
            QResizeEvent compact(QSize(860,570),QSize(1200,750));QApplication::sendEvent(&window,&compact);check(window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6),"Compact resize policy hides connection details");window.resize(860,570);
            last=std::chrono::steady_clock::now();measuring=true;
            widget<QPushButton>(window,"addButton")->click();auto* add=widget<QDialog>(window,"addDialog");
            widget<QLineEdit>(*add,"addUrl")->setText(url);widget<QLineEdit>(*add,"addDestination")->setText(QDir(root).filePath("original.bin"));widget<QLineEdit>(*add,"addFilename")->setText(QFileInfo(file).fileName());check(widget<QLineEdit>(*add,"addDestination")->text()==file,"Top name field sets manual download filename");widget<QLineEdit>(*add,"addSha256")->setText(sha);widget<QSpinBox>(*add,"addSpeedLimit")->setValue(256);widget<QPushButton>(*add,"addSubmit")->click();
            auto* table=widget<QTableWidget>(window,"downloadTable");wait([&]{return table->rowCount()==1 && window.current().entries[0].progress.downloadedBytes>100000;});table->selectRow(0);
            if(window.current().entries[0].status!=downshift::QueueStatus::Running)
                std::cerr<<"Observed queue status="<<static_cast<int>(window.current().entries[0].status)<<" error="<<window.current().entries[0].error.toStdString()<<'\n';
            check(window.current().entries[0].status==downshift::QueueStatus::Running,"GUI must show real running worker");check(widget<QLabel>(window,"detail0")->text()==url,"Source details");
            check(window.grab().save(QDir(shots).filePath("light.bmp"),"BMP"),"Light render");
            widget<QPushButton>(window,"pauseButton")->click();wait([&]{return window.current().entries[0].status==downshift::QueueStatus::Paused;});check(widget<QPushButton>(window,"startButton")->isEnabled(),"Resume control");
            widget<QLineEdit>(window,"searchField")->setText("absent-search");check(table->isRowHidden(0),"Search hides mismatch");widget<QLineEdit>(window,"searchField")->clear();table->selectRow(0);
            widget<QPushButton>(window,"startButton")->click();wait([&]{return window.current().entries[0].status==downshift::QueueStatus::Completed;});
            measuring=false;
            check(window.current().entries[0].progress.downloadedBytes==2*1024*1024,"Completed byte count");check(!widget<QPushButton>(window,"startButton")->isEnabled(),"Completed control disabled");
            const auto navigation=window.findChildren<QPushButton*>("navigation");navigation[3]->click();check(table->isRowHidden(0),"Attention page");navigation[2]->click();check(!table->isRowHidden(0),"Completed page");table->selectRow(0);
            widget<QPushButton>(window,"settingsButton")->click();auto* settings=widget<QDialog>(window,"settingsDialog");auto* sections=widget<QListWidget>(*settings,"settingsSections");for(int section=0;section<5;++section){sections->setCurrentRow(section);QApplication::processEvents();check(settings->grab().save(QDir(shots).filePath(QString("settings-light-%1.bmp").arg(section)),"BMP"),"Settings section rendered");}widget<QLineEdit>(*settings,"settingsDirectory")->setText(root);widget<QComboBox>(*settings,"settingsTheme")->setCurrentIndex(1);auto* simultaneous=widget<QComboBox>(*settings,"settingsSimultaneous");simultaneous->setCurrentIndex(simultaneous->findData(2));widget<QCheckBox>(*settings,"settingsNotifications")->setChecked(false);widget<QCheckBox>(*settings,"settingsDiagnostics")->setChecked(true);check(settings->grab().save(QDir(shots).filePath("settings-dark.bmp"),"BMP"),"Dark Settings rendered");widget<QPushButton>(*settings,"settingsSave")->click();
            wait([&]{return window.current().settings.dark;});check(window.current().settings.simultaneous==2,"Settings connected");check(window.grab().save(QDir(shots).filePath("dark.bmp"),"BMP"),"Dark render");
            widget<QPushButton>(window,"settingsButton")->click();settings=widget<QDialog>(window,"settingsDialog");widget<QComboBox>(*settings,"settingsTheme")->setCurrentIndex(0);settings->reject();check(window.current().settings.dark,"Cancel preserves settings");
            window.openAdd();add=widget<QDialog>(window,"addDialog");widget<QLineEdit>(*add,"addUrl")->setText("file:///C:/private");widget<QLineEdit>(*add,"addDestination")->setText(QDir(root).filePath("invalid.bin"));widget<QPushButton>(*add,"addSubmit")->click();wait([&]{return widget<QLabel>(window,"banner")->isVisible() && widget<QLabel>(window,"banner")->property("error").toBool();});check(window.current().entries.size()==1,"Invalid input creates no download");add->reject();
            std::cout<<"Transfer heartbeat ticks="<<beats<<"; longest event gap="<<maximumGap<<" seconds\n";
            check(beats>200 && maximumGap<1.0,"Event loop responsiveness during actual transfer");
            std::filesystem::copy_file(std::filesystem::path(file.toStdWString()),std::filesystem::path(QDir(root).filePath("proof.bin").toStdWString()));
        }
        {
            downshift::QueueManager queue(database);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready();});check(window.current().settings.dark && window.current().entries.size()==1,"Settings/history reopening");check(window.current().entries[0].status==downshift::QueueStatus::Completed,"Completed history reopening");check(window.current().entries[0].progress.averageBytesPerSecond>0 && window.current().entries[0].progress.elapsedSeconds>0,"History retains worker metrics");
            auto future=queue.deleteCompleted(window.current().entries[0].id);wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;});future.get();wait([&]{return window.current().entries.empty();});check(!QFileInfo::exists(file),"Delete worker removes completed file");
        }
        {
            const auto viewDatabase=QDir(root).filePath("view-fixtures.sqlite");
            {
                downshift::QueueStore store(viewDatabase);
                for(int index=0;index<4;++index){downshift::QueueEntry row;row.id=QString::number(index).rightJustified(32,'0');row.url="https://example.invalid/view-fixture";row.destination=QDir(root).filePath(QString("view-%1.bin").arg(index));row.created=index==0 ? 100:index==1 ? 200:300;row.updated=row.created;row.status=index<2 ? downshift::QueueStatus::Completed:index==2 ? downshift::QueueStatus::Failed:downshift::QueueStatus::Paused;row.progress.totalBytes=index==1 ? 1000:index==2 ? 700:200;store.save(row.id,QJsonDocument(row.json()).toJson(QJsonDocument::Compact));}
            }
            downshift::QueueManager queue(viewDatabase);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready() && window.current().entries.size()==4;});
            auto* table=widget<QTableWidget>(window,"downloadTable");auto* view=widget<QComboBox>(window,"viewDownloads");
            check(!table->cellWidget(0,3),"Progress is painted without per-row widgets");check(!widget<QWidget>(window,"detailsPanel")->isVisible(),"Unselected details do not consume permanent space");
            const auto rowId=[&](int row){return table->item(row,0)->data(Qt::UserRole).toString().right(1);};
            check(rowId(0)=="3","Newest first, including creation-order ties");table->selectRow(0);view->setCurrentIndex(1);check(rowId(0)=="0","Oldest first");check(rowId(table->currentRow())=="3","Sort preserves selection by download ID");view->setCurrentIndex(2);check(rowId(0)=="1","Largest known size first");
            const auto navigation=window.findChildren<QPushButton*>("navigation");
            for(int page=0;page<4;++page){navigation[page]->click();for(int mode=0;mode<3;++mode){view->setCurrentIndex(mode);int visible=0;for(int row=0;row<4;++row)if(!table->isRowHidden(row)){++visible;const auto id=rowId(row);check(page==0 || (page==1 && id=="3") || (page==2 && (id=="0" || id=="1")) || (page==3 && id=="2"),"Sorted view respects sidebar page");}check(visible==(page==0 ? 4:page==2 ? 2:1),"Page visible count survives all sorts");}}
            navigation[0]->click();view->setCurrentIndex(0);widget<QLineEdit>(window,"searchField")->setText("view-3.bin");check(!table->isRowHidden(0) && table->isRowHidden(1),"Search follows sorted download IDs");widget<QLineEdit>(window,"searchField")->clear();
            view->showPopup();QApplication::processEvents();check(view->view()->grab().save(QDir(shots).filePath("view-popup.bmp"),"BMP"),"View popup render");check(view->view()->window()->grab().save(QDir(shots).filePath("view-popup-window.bmp"),"BMP"),"Whole popup container render");check(qobject_cast<QFrame*>(view->view()->parentWidget())->frameShape()==QFrame::NoFrame && view->view()->window()->windowFlags().testFlag(Qt::FramelessWindowHint),"Qt and native popup frames are removed");view->hidePopup();check(window.grab().save(QDir(shots).filePath("sorted-history.bmp"),"BMP"),"Sorted history render");
            QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);QApplication::sendEvent(view,&down);check(view->currentIndex()==1,"Sort keyboard navigation");view->setCurrentIndex(0);
            for(const auto& dimensions:std::vector<QSize>{{1000,650},{900,600},{800,550}}){window.resize(dimensions);QApplication::processEvents();auto* search=widget<QLineEdit>(window,"searchField");check(search->width()>=180 && view->width()<160,"Search priority and compact sort width");check(table->horizontalScrollBar()->maximum()==0,"No unnecessary horizontal scroll at supported sizes");auto* primary=widget<QPushButton>(window,"addButton");check(window.rect().contains(primary->mapTo(&window,primary->rect().bottomRight())),"Primary action remains in the window");check(window.grab().save(QDir(shots).filePath(QString("main-%1x%2.bmp").arg(dimensions.width()).arg(dimensions.height())),"BMP"),"Responsive render");}
            window.showMaximized();QApplication::processEvents();check(window.grab().save(QDir(shots).filePath("maximized.bmp"),"BMP"),"Maximized render");window.showNormal();
            auto unusual=queue.snapshot().get().settings;unusual.simultaneous=7;unusual.perHost=6;unusual.hostConnections=5;unusual.automatic=false;unusual.connections=3;unusual.speedLimit=1;queue.configure(unusual).get();wait([&]{return window.current().settings.speedLimit==1;});window.openSettings();auto* values=widget<QDialog>(window,"settingsDialog");check(widget<QComboBox>(*values,"settingsSimultaneous")->currentData().toInt()==7 && widget<QComboBox>(*values,"settingsConnections")->currentData().toInt()==3,"Existing values absent from presets are preserved");auto* speedField=widget<QLineEdit>(*values,"settingsSpeedLimit");const auto exact=speedField->text();speedField->setText("invalid");widget<QPushButton>(*values,"settingsSave")->click();check(!widget<QLabel>(*values,"settingsError")->text().isEmpty() && queue.snapshot().get().settings.speedLimit==1,"Invalid settings remain open without saving");speedField->setText(exact);widget<QPushButton>(*values,"settingsSave")->click();wait([&]{return !window.findChild<QDialog*>("settingsDialog");});check(queue.snapshot().get().settings.json()==unusual.json(),"Saving unmodified settings preserves exact custom values and sub-KB speed limits");
            window.openSettings();auto* dialog=widget<QDialog>(window,"settingsDialog");auto* navigationSections=widget<QListWidget>(*dialog,"settingsSections");for(auto dimensions:std::vector<QSize>{{780,560},{630,460}}){dialog->resize(dimensions);for(int section=0;section<5;++section){navigationSections->setCurrentRow(section);QApplication::processEvents();auto* save=widget<QPushButton>(*dialog,"settingsSave");check(dialog->rect().contains(save->mapTo(dialog,save->rect().bottomRight())),"Settings buttons remain accessible at small sizes");}}dialog->reject();
        }
        {
            // A competing queue reproduces a real loading error without touching user data.
            const auto locked=QDir(root).filePath("locked-history.sqlite");auto owner=std::make_unique<downshift::QueueManager>(locked);owner->snapshot().get();const auto id=owner->add(url,QDir(root).filePath("held.bin"),0,false).get();downshift::QueueManager blocked(locked);downshift::MainWindow window(blocked);window.show();wait([&]{return widget<QLabel>(window,"emptyState")->text().contains("could not");});check(!window.ready(),"Failed load cannot become a valid empty history");check(!widget<QPushButton>(window,"addButton")->isEnabled(),"Failed load cannot create a replacement queue");check(widget<QLabel>(window,"summary")->text()=="History unavailable","Storage error is distinct from zero total");check(window.grab().save(QDir(shots).filePath("history-unavailable.bmp"),"BMP"),"Explicit history failure render");window.openSettings();auto* dialog=widget<QDialog>(window,"settingsDialog");check(!widget<QPushButton>(*dialog,"settingsSave")->isEnabled(),"Unavailable settings cannot overwrite saved preferences");dialog->reject();owner.reset();widget<QPushButton>(window,"retryHistory")->click();wait([&]{return window.ready();});check(window.current().entries.size()==1 && window.current().entries[0].id==id && window.current().entries[0].status==downshift::QueueStatus::Paused,"Retry recovers existing paused history after a temporary ownership error");
        }
        std::cout<<"GUI checks passed: "<<checks<<"; heartbeat ticks="<<beats<<"; longest event gap="<<maximumGap<<" seconds\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"GUI test failed: "<<e.what()<<"\n";return 1;}
}
