#include "gui/MainWindow.h"
#include "gui/Brand.h"
#include "core/Runtime.h"
#include "database/QueueStore.h"
#include "packaging/MsixSetup.h"
#include "TestWindows.h"
#include <QScreen>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QJsonDocument>
#include <QAbstractItemView>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMessageBox>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <array>
#include <iostream>
#include <stdexcept>

// The QtTest mouse helper delivers through Qt's window input path, which updates
// the global modifier state used when QAbstractItemView extends a selection.
QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow*,const QPointF&,const QPointF&,Qt::MouseButtons,
                                     Qt::MouseButton,QEvent::Type,Qt::KeyboardModifiers,int);
QT_END_NAMESPACE

namespace {
int checks=0;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message);++checks; }
template<class T> T* widget(QObject& parent,const char* name) { auto* result=parent.findChild<T*>(name); if(!result)throw std::runtime_error(std::string("Missing GUI control: ")+name);return result; }
QPushButton* navigationButton(QWidget& window,const QString& title) {
    for(auto* button:window.findChildren<QPushButton*>("navigation"))
        if(button->accessibleName()==title)return button;
    throw std::runtime_error("Missing sidebar page: "+title.toStdString());
}
template<class F> void wait(F condition,int timeout=15000) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(std::chrono::steady_clock::now()<end) {QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(condition())return;QThread::msleep(5);}
    throw std::runtime_error("GUI condition timed out");
}
int testFirstLaunch(bool testChrome) {
    using namespace downshift::msix;
    const QDir tools(mediaToolsDirectory());
    check(tools.path().contains("/test-video-support/") || familyName().startsWith("UshaLabs.Downshift.SetupValidation_"),
        "First launch verification requires an isolated installation");
    check(!tools.exists("installed-files.json"),"No media tools are installed before first launch");
    QTimer controls;
    bool sawGate=false, sawDark=false, sawDownloadSize=false, sawChromeSteps=false, chromeStarted=false, chromeFinished=false, retryOffered=false, repeatedOpenBlocked=false, sawBrowsers=false;
    controls.setInterval(30);
    QObject::connect(&controls,&QTimer::timeout,&controls,[&]{
        for(auto* window:QApplication::topLevelWidgets()){
            if(window->objectName()!="firstLaunchSetup")continue;
            auto* dialog=static_cast<QDialog*>(window);
            sawDark=qApp->property("downshiftDark").toBool();
            const auto size=widget<QLabel>(*dialog,"videoSetupDownloadSize")->text();
            if(size.contains(" of ") && size.contains("%") && size.contains(" remaining"))sawDownloadSize=true;
            if(auto* steps=dialog->findChild<QLabel*>("chromeSetupSteps")){
                const auto text=steps->text();
                sawChromeSteps=text.contains("1. Click Open Chrome Extensions") && text.contains("2. Click Load unpacked") && text.contains("3. Press Ctrl+V");
            }
            if(!sawGate){
                auto* browsers=widget<QComboBox>(*dialog,"setupBrowser");
                sawBrowsers=browsers->count()==4 && browsers->findText("Edge")>=0 && browsers->findText("Opera")>=0;
                browsers->setCurrentText("Firefox");
                sawBrowsers=sawBrowsers && widget<QLabel>(*dialog,"chromeSetupSteps")->text().contains("Mozilla-signed") && widget<QLabel>(*dialog,"chromeSetupSteps")->text().contains("Temporary Add-on");
                browsers->setCurrentText("Chrome");
                dialog->reject();
                sawGate=dialog->isVisible() && QApplication::activeModalWidget()==dialog;
                repeatedOpenBlocked=!finishFirstLaunchSetup() && QApplication::activeModalWidget()==dialog;
                if(testChrome){
                    auto* chrome=widget<QPushButton>(*dialog,"setupChromeButton");
                    chromeStarted=chrome->isEnabled();
                    chrome->click();
                }
            }
            if(testChrome && QApplication::clipboard()->text().endsWith("\\extension"))chromeFinished=true;
            if(widget<QPushButton>(*dialog,"retryVideoSetup")->isVisible()){
                retryOffered=true;
                dialog->done(QDialog::Rejected);
            }
        }
    });
    controls.start();
    check(finishFirstLaunchSetup(),"First launch finishes only after the verified install");
    controls.stop();
    check(sawGate && !retryOffered,"The setup screen was modal, visible and could not be dismissed");
    check(repeatedOpenBlocked,"Repeated tray or browser activation keeps a single first-launch setup dialog");
    check(sawBrowsers,"Browser setup offers Chrome, Edge, Firefox and Opera with Firefox-specific installation steps");
    check(sawDark,"First-launch video setup uses the default dark appearance");
    check(sawDownloadSize && sawChromeSteps,"First launch shows download counts, remaining size and the three Chrome steps");
    if(testChrome){
        // A fast tool install may close the dialog as soon as the Chrome action completes.
        check(chromeStarted && (chromeFinished || QApplication::clipboard()->text().endsWith("\\extension")),
            "Chrome setup could be started before the video tools were ready");
    }
    check(tools.exists("installed-files.json") && tools.exists("ffmpeg.exe"),"The verified install is in writable user storage");
    QTimer reopened;
    bool unexpectedPrompt=false;
    QObject::connect(&reopened,&QTimer::timeout,&reopened,[&]{
        for(auto* window:QApplication::topLevelWidgets()){
            if(window->objectName()=="firstLaunchSetup"){
                unexpectedPrompt=true;
                static_cast<QDialog*>(window)->done(QDialog::Rejected);
            }
        }
    });
    reopened.start(30);
    check(finishFirstLaunchSetup() && !unexpectedPrompt,"Later launches skip the setup screen");
    std::cout<<"First launch setup checks passed: "<<checks<<'\n';
    return 0;
}

}
int main(int argc,char** argv) {
    QApplication app(argc,argv); QApplication::setApplicationName("Downshift");QApplication::setOrganizationName("Usha Labs");QApplication::setApplicationVersion("0.4.2");
    HiddenTestWindows hidden;app.installEventFilter(&hidden);std::cout<<"GUI device pixel ratio: "<<app.primaryScreen()->devicePixelRatio()<<'\n';
    if(app.arguments().contains("--first-launch-setup")){
        try{return testFirstLaunch(app.arguments().contains("--mock-chrome"));}
        catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if(argc!=5)return 2;const auto root=QString::fromLocal8Bit(argv[1]),url=QString::fromLatin1(argv[2]),sha=QString::fromLatin1(argv[3]),shots=QString::fromLocal8Bit(argv[4]);
    QDir().mkpath(shots);downshift::CurlRuntime curl;const auto database=QDir(root).filePath("queue.sqlite"),file=QDir(root).filePath("Verified download.bin");
    int beats=0;double maximumGap=0;bool measuring=false;auto last=std::chrono::steady_clock::now();QTimer heartbeat;heartbeat.setInterval(20);QObject::connect(&heartbeat,&QTimer::timeout,[&]{const auto now=std::chrono::steady_clock::now();if(measuring){maximumGap=std::max(maximumGap,std::chrono::duration<double>(now-last).count());++beats;}last=now;});heartbeat.start();
    try {
        check(!downshift::brandIcon().pixmap(32,32).isNull(),"Brand icon decodes with the shipped Qt image features");
        check(!QPixmap(":/branding/chevron.xpm").isNull(),"Quality selector arrow decodes with shipped Qt");
        {
            downshift::QueueManager queue(database);downshift::MainWindow window(queue);check(widget<QLabel>(window,"emptyState")->text().contains("Loading"),"Startup cannot falsely display empty history before loading");
            check(qApp->property("downshiftDark").toBool(),"The loading window starts in dark mode");
            wait([&]{return window.ready();});
            check(!window.isVisible(),"Loading history in the background does not show the main window");
            widget<QSystemTrayIcon>(window,"systemTray")->activated(QSystemTrayIcon::Trigger);
            check(window.isVisible(),"Clicking the system tray icon opens the hidden main window");
            window.hide();window.showDownloads();
            check(window.isVisible(),"Opening an existing background instance restores its main window");
            check(window.current().settings.dark,"Dark is the fresh profile default");
            check(downshift::QueueSettings::fromJson({}).dark,"Older settings without an appearance use the default");
            auto light=window.current().settings;light.dark=false;queue.configure(light).get();
            wait([&]{return !window.current().settings.dark && !qApp->property("downshiftDark").toBool();});
            check(window.width()<=1075 && window.height()<=630 && window.width()>window.height()*1.4,"Startup uses the compact landscape window");
            auto legacy=window.current().settings.json();legacy.remove("concurrencyVersion");legacy["simultaneous"]=2;legacy["perHost"]=2;legacy["hostConnections"]=16;
            const auto upgraded=downshift::QueueSettings::fromJson(legacy);
            check(upgraded.simultaneous==0&&upgraded.perHost==0&&upgraded.hostConnections==0&&upgraded.directory==window.current().settings.directory,"Existing profiles upgrade to Unlimited without changing the destination");
            window.resize(860,570);QApplication::processEvents();
            last=std::chrono::steady_clock::now();measuring=true;
            widget<QPushButton>(window,"addButton")->click();auto* add=widget<QDialog>(window,"addDialog");
            widget<QLineEdit>(*add,"addUrl")->setText(QUrl(url).resolved(QUrl("report.pdf")).toString());
            check(widget<QLineEdit>(*add,"addDestination")->text()==QDir(window.current().settings.directory).filePath("Docs/report.pdf"),
                "Manual document download defaults to Docs");
            widget<QLineEdit>(*add,"addUrl")->setText(url);
            auto* size=widget<QLabel>(*add,"addSize");
            wait([&]{return size->text().startsWith("2")&&size->text().contains("MiB");});
            check(window.current().entries.empty(),"Range metadata reveals the full file size before starting a download");
            widget<QLineEdit>(*add,"addDestination")->setText(QDir(root).filePath("original.bin"));widget<QLineEdit>(*add,"addFilename")->setText(QFileInfo(file).fileName());check(widget<QLineEdit>(*add,"addDestination")->text()==file,"Top name field sets manual download filename");widget<QLineEdit>(*add,"addSha256")->setText(sha);widget<QSpinBox>(*add,"addSpeedLimit")->setValue(256);widget<QPushButton>(*add,"addSubmit")->click();
            auto* table=widget<QTableWidget>(window,"downloadTable");wait([&]{return table->rowCount()==1 && window.current().entries[0].progress.downloadedBytes>100000;});
            check(!table->isColumnHidden(5)&&table->isColumnHidden(6),"Compact window keeps ETA and defers optional connection count");
            check(table->horizontalScrollBar()->maximum()==0,"Compact transfer list fits the window");table->selectRow(0);
            if(window.current().entries[0].status!=downshift::QueueStatus::Running)
                std::cerr<<"Observed queue status="<<static_cast<int>(window.current().entries[0].status)<<" error="<<window.current().entries[0].error.toStdString()<<'\n';
            check(window.current().entries[0].status==downshift::QueueStatus::Running,"GUI must show real running worker");check(widget<QLabel>(window,"detail0")->text()==url,"Source details");
            check(widget<QLabel>(window,"dataMetric")->text()==widget<QLabel>(window,"detail3")->text(),"Selected total follows actual in-progress bytes");
            check(window.grab().save(QDir(shots).filePath("light.bmp"),"BMP"),"Light render");
            widget<QPushButton>(window,"pauseButton")->click();wait([&]{return window.current().entries[0].status==downshift::QueueStatus::Paused;});check(widget<QPushButton>(window,"startButton")->isEnabled(),"Resume control");
            widget<QLineEdit>(window,"searchField")->setText("absent-search");check(table->isRowHidden(0),"Search hides mismatch");widget<QLineEdit>(window,"searchField")->clear();table->selectRow(0);
            widget<QPushButton>(window,"startButton")->click();wait([&]{return window.current().entries[0].status==downshift::QueueStatus::Completed;});
            measuring=false;
            check(window.current().entries[0].progress.downloadedBytes==2*1024*1024,"Completed byte count");check(!widget<QPushButton>(window,"startButton")->isEnabled(),"Completed control disabled");
            navigationButton(window,"Needs attention")->click();check(table->isRowHidden(0),"Attention page");navigationButton(window,"Completed")->click();check(!table->isRowHidden(0),"Completed page");table->selectRow(0);
            widget<QPushButton>(window,"settingsButton")->click();auto* settings=widget<QDialog>(window,"settingsDialog");auto* sections=widget<QListWidget>(*settings,"settingsSections");for(int section=0;section<5;++section){sections->setCurrentRow(section);QApplication::processEvents();check(settings->grab().save(QDir(shots).filePath(QString("settings-light-%1.bmp").arg(section)),"BMP"),"Settings section rendered");}widget<QLineEdit>(*settings,"settingsDirectory")->setText(root);widget<QComboBox>(*settings,"settingsTheme")->setCurrentIndex(1);auto* simultaneous=widget<QSpinBox>(*settings,"settingsSimultaneous");check(simultaneous->specialValueText()=="Unlimited"&&simultaneous->value()==0,"Settings shows Unlimited by default");simultaneous->setValue(2);widget<QCheckBox>(*settings,"settingsNotifications")->setChecked(false);widget<QCheckBox>(*settings,"settingsDiagnostics")->setChecked(true);check(settings->grab().save(QDir(shots).filePath("settings-dark.bmp"),"BMP"),"Dark Settings rendered");widget<QPushButton>(*settings,"settingsSave")->click();
            wait([&]{return window.current().settings.dark;});check(window.current().settings.simultaneous==2,"Settings connected");check(window.grab().save(QDir(shots).filePath("dark.bmp"),"BMP"),"Dark render");
            widget<QPushButton>(window,"settingsButton")->click();settings=widget<QDialog>(window,"settingsDialog");widget<QComboBox>(*settings,"settingsTheme")->setCurrentIndex(0);settings->reject();check(window.current().settings.dark,"Cancel preserves settings");
            window.openAdd();add=widget<QDialog>(window,"addDialog");widget<QLineEdit>(*add,"addUrl")->setText("file:///C:/private");widget<QLineEdit>(*add,"addDestination")->setText(QDir(root).filePath("invalid.bin"));widget<QPushButton>(*add,"addSubmit")->click();wait([&]{return widget<QLabel>(window,"banner")->isVisible() && widget<QLabel>(window,"banner")->property("error").toBool();});check(window.current().entries.size()==1,"Invalid input creates no download");add->reject();
            std::cout<<"Transfer heartbeat ticks="<<beats<<"; longest event gap="<<maximumGap<<" seconds\n";
            check(beats>200 && maximumGap<1.0,"Event loop responsiveness during actual transfer");
            std::filesystem::copy_file(std::filesystem::path(file.toStdWString()),std::filesystem::path(QDir(root).filePath("proof.bin").toStdWString()));
        }
        {
            downshift::QueueManager queue(database);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready();});check(window.current().settings.dark && window.current().entries.size()==1,"Settings/history reopening");check(window.current().entries[0].status==downshift::QueueStatus::Completed,"Completed history reopening");check(window.current().entries[0].progress.averageBytesPerSecond>0 && window.current().entries[0].progress.elapsedSeconds>0,"History retains worker metrics");
            auto* table=widget<QTableWidget>(window,"downloadTable");table->selectRow(0);
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(table,&key);
            widget<QMessageBox>(window,"deleteDownloadsConfirmation")->button(QMessageBox::Yes)->click();
            wait([&]{return window.current().entries.empty();});check(!QFileInfo::exists(file),"Delete key removes a completed file and history");
        }
        {
            const auto viewDatabase=QDir(root).filePath("view-fixtures.sqlite");
            {
                downshift::QueueStore store(viewDatabase);
                for(int index=0;index<4;++index){downshift::QueueEntry row;row.id=QString::number(index).rightJustified(32,'0');row.url="https://example.invalid/view-fixture";row.destination=QDir(root).filePath(QString("view-%1.bin").arg(index));row.created=index==0 ? 100:index==1 ? 200:300;row.updated=row.created;row.status=index<2 ? downshift::QueueStatus::Completed:index==2 ? downshift::QueueStatus::Failed:downshift::QueueStatus::Paused;row.progress.totalBytes=index==1 ? 1000:index==2 ? 700:200;row.progress.downloadedBytes=index<2?100*(index+1):index==3?50:0;
                    if(index<2){QFile payload(row.destination);check(payload.open(QIODevice::WriteOnly),"Create owned completed fixture");payload.write(QByteArray(static_cast<qsizetype>(row.progress.downloadedBytes),'x'));}
                    store.save(row.id,QJsonDocument(row.json()).toJson(QJsonDocument::Compact));}
            }
            downshift::QueueManager queue(viewDatabase);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready() && window.current().entries.size()==4;});
            auto* table=widget<QTableWidget>(window,"downloadTable");auto* view=widget<QComboBox>(window,"viewDownloads");
            check(!table->cellWidget(0,3),"Progress is painted without per-row widgets");check(!widget<QWidget>(window,"detailsPanel")->isVisible(),"Unselected details do not consume permanent space");
            const auto rowId=[&](int row){return table->item(row,0)->data(Qt::UserRole).toString().right(1);};
            check(rowId(0)=="3","Newest first, including creation-order ties");table->selectRow(0);view->setCurrentIndex(1);check(rowId(0)=="0","Oldest first");check(rowId(table->currentRow())=="3","Sort preserves selection by download ID");view->setCurrentIndex(2);check(rowId(0)=="1","Largest known size first");
            const std::array navigation={navigationButton(window,"All downloads"),navigationButton(window,"In progress"),
                navigationButton(window,"Completed"),navigationButton(window,"Needs attention")};
            for(int page=0;page<4;++page){navigation[page]->click();for(int mode=0;mode<3;++mode){view->setCurrentIndex(mode);int visible=0;for(int row=0;row<4;++row)if(!table->isRowHidden(row)){++visible;const auto id=rowId(row);check(page==0 || (page==1 && id=="3") || (page==2 && (id=="0" || id=="1")) || (page==3 && id=="2"),"Sorted view respects sidebar page");}check(visible==(page==0 ? 4:page==2 ? 2:1),"Page visible count survives all sorts");}}
            navigation[0]->click();view->setCurrentIndex(0);widget<QLineEdit>(window,"searchField")->setText("view-3.bin");check(!table->isRowHidden(0) && table->isRowHidden(1),"Search follows sorted download IDs");widget<QLineEdit>(window,"searchField")->clear();
            view->showPopup();QApplication::processEvents();check(view->view()->grab().save(QDir(shots).filePath("view-popup.bmp"),"BMP"),"View popup render");check(view->view()->window()->grab().save(QDir(shots).filePath("view-popup-window.bmp"),"BMP"),"Whole popup container render");check(qobject_cast<QFrame*>(view->view()->parentWidget())->frameShape()==QFrame::NoFrame && view->view()->window()->windowFlags().testFlag(Qt::FramelessWindowHint),"Qt and native popup frames are removed");view->hidePopup();check(window.grab().save(QDir(shots).filePath("sorted-history.bmp"),"BMP"),"Sorted history render");
            QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);QApplication::sendEvent(view,&down);check(view->currentIndex()==1,"Sort keyboard navigation");view->setCurrentIndex(0);
            for(const auto& dimensions:std::vector<QSize>{{1000,650},{900,600},{800,550}}){window.resize(dimensions);QApplication::processEvents();auto* search=widget<QLineEdit>(window,"searchField");check(search->width()>=180 && view->width()<160,"Search priority and compact sort width");check(table->horizontalScrollBar()->maximum()==0,"No unnecessary horizontal scroll at supported sizes");auto* primary=widget<QPushButton>(window,"addButton");check(window.rect().contains(primary->mapTo(&window,primary->rect().bottomRight())),"Primary action remains in the window");check(window.grab().save(QDir(shots).filePath(QString("main-%1x%2.bmp").arg(dimensions.width()).arg(dimensions.height())),"BMP"),"Responsive render");}
            window.showMaximized();QApplication::processEvents();check(window.grab().save(QDir(shots).filePath("maximized.bmp"),"BMP"),"Maximized render");window.showNormal();
            auto unusual=queue.snapshot().get().settings;unusual.simultaneous=37;unusual.perHost=31;unusual.hostConnections=96;unusual.automatic=false;unusual.connections=3;unusual.speedLimit=1;queue.configure(unusual).get();wait([&]{return window.current().settings.speedLimit==1;});window.openSettings();auto* values=widget<QDialog>(window,"settingsDialog");check(widget<QSpinBox>(*values,"settingsSimultaneous")->value()==37 && widget<QSpinBox>(*values,"settingsPerHost")->value()==31 && widget<QSpinBox>(*values,"settingsHostConnections")->value()==96 && widget<QComboBox>(*values,"settingsConnections")->currentData().toInt()==3,"Existing values absent from presets are preserved");auto* speedField=widget<QLineEdit>(*values,"settingsSpeedLimit");const auto exact=speedField->text();speedField->setText("invalid");widget<QPushButton>(*values,"settingsSave")->click();check(!widget<QLabel>(*values,"settingsError")->text().isEmpty() && queue.snapshot().get().settings.speedLimit==1,"Invalid settings remain open without saving");speedField->setText(exact);widget<QPushButton>(*values,"settingsSave")->click();wait([&]{return !window.findChild<QDialog*>("settingsDialog");});check(queue.snapshot().get().settings.json()==unusual.json(),"Saving unmodified settings preserves exact custom values and sub-KB speed limits");
            window.openSettings();auto* dialog=widget<QDialog>(window,"settingsDialog");auto* navigationSections=widget<QListWidget>(*dialog,"settingsSections");for(auto dimensions:std::vector<QSize>{{780,560},{630,460}}){dialog->resize(dimensions);for(int section=0;section<5;++section){navigationSections->setCurrentRow(section);QApplication::processEvents();auto* save=widget<QPushButton>(*dialog,"settingsSave");check(dialog->rect().contains(save->mapTo(dialog,save->rect().bottomRight())),"Settings buttons remain accessible at small sizes");}}dialog->reject();
            navigation[0]->click();view->setCurrentIndex(0);
            int timestamp=1000;
            const auto click=[&](QWidget* widget,const QPoint& point,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
                const auto local=widget->mapTo(&window,point);
                const auto screen=widget->mapToGlobal(point);
                qt_handleMouseEvent(window.windowHandle(),local,screen,Qt::LeftButton,Qt::LeftButton,QEvent::MouseButtonPress,modifiers,timestamp);
                qt_handleMouseEvent(window.windowHandle(),local,screen,Qt::NoButton,Qt::LeftButton,QEvent::MouseButtonRelease,modifiers,timestamp+1);
                timestamp+=1000;
            };
            const auto clickRow=[&](int row,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
                table->scrollToItem(table->item(row,0));QApplication::processEvents();
                click(table->viewport(),table->visualItemRect(table->item(row,0)).center(),modifiers);
            };
            auto* total=widget<QLabel>(window,"dataMetric");
            table->clearSelection();check(total->text()=="350 B","No selection shows all retained bytes");
            clickRow(2);check(total->text()=="200 B","Single selection shows only downloaded bytes");
            clickRow(0,Qt::ControlModifier);check(total->text()=="250 B"&&table->selectionModel()->selectedRows().size()==2,"Ctrl-click sums non-adjacent rows");
            clickRow(0);check(total->text()=="50 B"&&table->selectionModel()->selectedRows().size()==1,"Plain click on a selected row leaves only that row selected");
            clickRow(2,Qt::ControlModifier);
            auto* sidebar=widget<QWidget>(window,"sidebar");
            click(sidebar,QPoint(2,sidebar->height()/2));
            check(!table->selectionModel()->hasSelection()&&total->text()=="350 B","Clicking blank sidebar space restores the overall total");
            clickRow(2);clickRow(0,Qt::ControlModifier);
            const QPoint blank(10,table->viewport()->height()-5);
            check(!table->indexAt(blank).isValid(),"Test point lies below the final download");
            click(table->viewport(),blank);
            check(!table->selectionModel()->hasSelection()&&!table->currentIndex().isValid()&&total->text()=="350 B","Clicking below the list clears selection and focus row");
            clickRow(1);QThread::msleep(350);QApplication::processEvents();
            check(table->currentRow()==1&&table->selectionModel()->selectedRows().size()==1,"Shift anchor remains selected through refresh");
            clickRow(3,Qt::ShiftModifier);
            if(total->text()!="300 B"||table->selectionModel()->selectedRows().size()!=3){
                std::cerr<<"Shift selection total="<<total->text().toStdString()<<" current="<<table->currentRow()<<" rows:";
                for(const auto& selected:table->selectionModel()->selectedRows())std::cerr<<' '<<selected.row();
                std::cerr<<'\n';
            }
            check(total->text()=="300 B"&&table->selectionModel()->selectedRows().size()==3,"Shift range survives a live refresh");
            view->setCurrentIndex(1);check(table->selectionModel()->selectedRows().size()==3&&total->text()=="300 B","Sort preserves all selected IDs");
            navigation[2]->click();check(table->selectionModel()->selectedRows().size()==2&&total->text()=="300 B","Filtered-out rows are no longer selected");
            QKeyEvent deleteKey(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(table,&deleteKey);
            auto* confirm=widget<QMessageBox>(window,"deleteDownloadsConfirmation");
            check(confirm->text().contains("2 selected downloads"),"Delete confirms the complete selection once");
            confirm->button(QMessageBox::Cancel)->click();wait([&]{return !window.findChild<QMessageBox*>("deleteDownloadsConfirmation");});
            check(QFileInfo::exists(QDir(root).filePath("view-0.bin"))&&QFileInfo::exists(QDir(root).filePath("view-1.bin")),"Cancelling leaves both selected files");
            QApplication::sendEvent(table,&deleteKey);widget<QMessageBox>(window,"deleteDownloadsConfirmation")->button(QMessageBox::Yes)->click();
            wait([&]{return window.current().entries.size()==2;});
            check(!QFileInfo::exists(QDir(root).filePath("view-0.bin"))&&!QFileInfo::exists(QDir(root).filePath("view-1.bin")),"Confirmed batch deletes both selected files only");
        }
        {
            // A competing queue reproduces a real loading error without touching user data.
            const auto locked=QDir(root).filePath("locked-history.sqlite");auto owner=std::make_unique<downshift::QueueManager>(locked);owner->snapshot().get();const auto id=owner->add(url,QDir(root).filePath("held.bin"),0,false).get();downshift::QueueManager blocked(locked);downshift::MainWindow window(blocked);window.show();wait([&]{return widget<QLabel>(window,"emptyState")->text().contains("could not");});check(!window.ready(),"Failed load cannot become a valid empty history");check(!widget<QPushButton>(window,"addButton")->isEnabled(),"Failed load cannot create a replacement queue");check(widget<QLabel>(window,"totalMetric")->text()=="—","Storage error is distinct from zero total");check(window.grab().save(QDir(shots).filePath("history-unavailable.bmp"),"BMP"),"Explicit history failure render");window.openSettings();auto* dialog=widget<QDialog>(window,"settingsDialog");check(!widget<QPushButton>(*dialog,"settingsSave")->isEnabled(),"Unavailable settings cannot overwrite saved preferences");dialog->reject();owner.reset();widget<QPushButton>(window,"retryHistory")->click();wait([&]{return window.ready();});check(window.current().entries.size()==1 && window.current().entries[0].id==id && window.current().entries[0].status==downshift::QueueStatus::Paused,"Retry recovers existing paused history after a temporary ownership error");
        }
        std::cout<<"GUI checks passed: "<<checks<<"; heartbeat ticks="<<beats<<"; longest event gap="<<maximumGap<<" seconds\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"GUI test failed: "<<e.what()<<"\n";return 1;}
}
