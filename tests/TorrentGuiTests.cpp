#include "gui/MainWindow.h"
#include "gui/TorrentDialogs.h"
#include "core/Runtime.h"
#include "TestCrash.h"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace lt=libtorrent;
using namespace downshift;
using namespace downshift::torrent;
namespace {
// Hiding during Show reenters Qt's modal registration. Polish runs before the
// window becomes visible and keeps private fixtures off the physical desktop.
class HiddenTorrentWindows final:public QObject {
    bool eventFilter(QObject* object,QEvent* event)override{
        if(event->type()==QEvent::Polish)if(auto* widget=qobject_cast<QWidget*>(object);widget&&widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
int checks=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
template<class T>T* control(QObject& owner,const char* name){auto* value=owner.findChild<T*>(name);if(!value)throw std::runtime_error(std::string("Missing control: ")+name);return value;}
template<class F>void wait(F done,int seconds=20){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(!done()){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("Torrent GUI condition timed out");QApplication::processEvents();QThread::msleep(10);}}
template<class T>T complete(std::future<T> future){wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;},30);if constexpr(std::is_void_v<T>){future.get();}else return future.get();}
QByteArray digest(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Expected GUI payload is missing");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"Read independent hash");return h.result();}
Snapshot row(TorrentEngine& engine,QString id){for(const auto& item:complete(engine.snapshots()))if(item.id==id)return item;throw std::runtime_error("Missing GUI torrent ID");}
QString fixture(const QString& root,const QString& name="gui-payload"){
    std::vector<lt::create_file_entry> files;
    for(int n=0;n<2;++n){const auto relative=QString("%1/file%2.bin").arg(name).arg(n);const auto path=QDir(root).filePath("seed/"+relative);require(QDir().mkpath(QFileInfo(path).absolutePath()),"Private seed directory");QByteArray bytes(256*1024+101+17*n,0);for(qsizetype i=0;i<bytes.size();++i)bytes[i]=static_cast<char>((i*47+n*101)%251);QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Private deterministic GUI seed file");f.close();files.emplace_back(relative.toUtf8().toStdString(),bytes.size());}
    lt::create_torrent torrent(std::move(files),16384);lt::error_code error;lt::set_piece_hashes(torrent,QDir(root).filePath("seed").toUtf8().toStdString(),error);require(!error,"Native GUI fixture hashes");const auto bytes=torrent.generate_buf();const auto path=QDir(root).filePath("gui.torrent");QFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size()),"GUI torrent metadata fixture");return path;
}
int select(MainWindow& window,const QString& id){auto* table=control<QTableWidget>(window,"downloadTable");for(int n=0;n<table->rowCount();++n)if(table->item(n,0)->data(Qt::UserRole).toString()==id){table->selectRow(n);return n;}return -1;}
QString inspect(MainWindow& window,TorrentEngine& engine,const QString& source,const QString& destination,bool magnet=false){
    window.openTorrent();auto* dialog=control<QDialog>(window,"addTorrentDialog");control<QLineEdit>(*dialog,"torrentSource")->setText(source);control<QLineEdit>(*dialog,"torrentFolder")->setText(destination);control<QPushButton>(*dialog,"torrentInspect")->click();QString id;wait([&]{for(const auto& value:complete(engine.snapshots()))if(value.directory==QDir(destination).absolutePath())id=value.id;return !id.isEmpty();});if(!magnet)wait([&]{return control<QTableWidget>(*dialog,"torrentFiles")->rowCount()==2;});else wait([&]{const auto text=control<QLabel>(*dialog,"torrentMetadataStatus")->text();return text.contains("Finding metadata")||text.contains("Choose files");});return id;
}
void removeViaMenu(MainWindow& window,const QString& id,int choice){
    require(select(window,id)>=0,"Select torrent for removal");
    control<QPushButton>(window,"moreButton")->click();auto* menu=control<QMenu>(window,"torrentContextMenu");
    for(auto* action:menu->actions())if(action->text()=="Remove torrent…"){action->trigger();break;}
    wait([&]{return window.findChild<QMessageBox*>("torrentRemoveConfirmation")!=nullptr;});auto* box=control<QMessageBox>(window,"torrentRemoveConfirmation");require(box->defaultButton()==box->button(QMessageBox::Cancel),"Removal defaults to Cancel");
    if(choice==0)box->button(QMessageBox::Cancel)->click();else control<QPushButton>(*box,choice==1?"torrentRemoveKeep":"torrentRemoveDelete")->click();
    wait([&]{return !window.findChild<QMessageBox*>("torrentRemoveConfirmation");});
}

}
int main(int argc,char** argv){
    QApplication app(argc,argv);SetUnhandledExceptionFilter(recordTestCrash);app.setQuitOnLastWindowClosed(false);HiddenTorrentWindows hidden;app.installEventFilter(&hidden);QTemporaryDir temporary;
    try{
        CurlRuntime runtime;require(temporary.isValid(),"Private GUI queue");const auto root=temporary.path(),metadata=fixture(root),other=fixture(QDir(root).filePath("other"),"other-payload"),store=QDir(root).filePath("torrents.sqlite");const auto shots=argc>1?QString::fromLocal8Bit(argv[1]):QDir(root).filePath("shots");require(QDir().mkpath(shots),"GUI render destination");
        lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:6881");settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);settings.set_bool(lt::settings_pack::enable_outgoing_utp,false);settings.set_bool(lt::settings_pack::enable_incoming_utp,false);settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);settings.set_int(lt::settings_pack::min_reconnect_time,1);
        lt::session seed{lt::session_params(settings)};auto params=lt::load_torrent_file(metadata.toUtf8().toStdString());params.save_path=QDir(root).filePath("seed").toUtf8().toStdString();params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed|lt::torrent_flags::seed_mode);lt::error_code error;auto handle=seed.add_torrent(params,error);require(!error,"GUI peer seed added");wait([&]{return handle.status().is_seeding&&seed.listen_port()!=0;});
        QueueManager queue(QDir(root).filePath("queue.sqlite"));const auto http=queue.add("https://example.invalid/preserved.bin",QDir(root).filePath("preserved.bin"),0,false).get();const auto httpBefore=complete(queue.snapshot());
        int beats=0;double longest=0,unscoped=0;bool measuring=false;QString stage="initial window construction",slowest;auto last=std::chrono::steady_clock::now();QTimer heartbeat;heartbeat.setInterval(10);QObject::connect(&heartbeat,&QTimer::timeout,[&]{const auto now=std::chrono::steady_clock::now();const auto gap=std::chrono::duration<double>(now-last).count();if(gap>unscoped){unscoped=gap;slowest=stage;}if(measuring){longest=std::max(longest,gap);++beats;}last=now;});heartbeat.start();
        auto engine=std::make_unique<TorrentEngine>(Options{false,true,store});QString retained;
        {
            MainWindow window(queue);window.attachTorrents(*engine);window.show();wait([&]{return window.ready();});auto* table=control<QTableWidget>(window,"downloadTable");require(window.findChildren<QPushButton*>("navigation").size()==5,"Torrents sidebar joins existing pages");
            stage="input validation";window.openTorrent();auto* invalid=control<QDialog>(window,"addTorrentDialog");control<QLineEdit>(*invalid,"torrentSource")->setText("invalid");control<QLineEdit>(*invalid,"torrentFolder")->setText(QDir(root).filePath("invalid"));control<QPushButton>(*invalid,"torrentInspect")->click();wait([&]{return !control<QLabel>(*invalid,"torrentError")->text().isEmpty();});require(complete(engine->snapshots()).empty(),"Invalid metadata creates no retained entry");invalid->reject();wait([&]{return !window.findChild<QDialog*>("addTorrentDialog");});
            stage="waiting metadata preview";const auto waiting=inspect(window,*engine,"magnet:?xt=urn:btih:"+QString(40,'1'),QDir(root).filePath("waiting"),true);auto* waitingDialog=control<QDialog>(window,"addTorrentDialog");wait([&]{return control<QLabel>(*waitingDialog,"torrentMetadataStatus")->text().contains("Finding");});require(!row(*engine,waiting).metadata&&row(*engine,waiting).downloadedBytes==0,"Unknown magnet waits without payload");control<QPushButton>(*waitingDialog,"torrentAddCancel")->click();wait([&]{return !window.findChild<QDialog*>("addTorrentDialog");});require(complete(engine->snapshots()).empty(),"Cancel waiting preview removes its retained record");
            stage="file preview and screenshot export";const auto save=QDir(root).filePath("selected");const auto id=inspect(window,*engine,metadata,save);auto* add=control<QDialog>(window,"addTorrentDialog");require(row(*engine,id).metadataOnly&&row(*engine,id).downloadedBytes==0,"Preview does not download payload");auto* files=control<QTableWidget>(*add,"torrentFiles");qobject_cast<QComboBox*>(files->cellWidget(1,3))->setCurrentIndex(0);require(!control<QCheckBox>(*add,"torrentStart")->isChecked(),"Add paused is default");require(add->grab().save(QDir(shots).filePath("add-torrent.bmp"),"BMP"),"Add dialog rendered");control<QCheckBox>(*add,"torrentStart")->setChecked(true);control<QPushButton>(*add,"torrentConfirm")->click();wait([&]{return !window.findChild<QDialog*>("addTorrentDialog");});stage="selected native transfer";last=std::chrono::steady_clock::now();measuring=true;complete(engine->connectPeer(id,"127.0.0.1",seed.listen_port()));wait([&]{return row(*engine,id).state==State::Complete;});measuring=false;require(!row(*engine,id).metadataOnly&&row(*engine,id).files[1].priority==0,"Confirmed selection reaches native engine");require(digest(QDir(save).filePath("gui-payload/file0.bin"))==digest(QDir(root).filePath("seed/gui-payload/file0.bin")),"GUI selected transfer independent final SHA-256");
            stage="sorting and screenshot export";wait([&]{const auto selected=select(window,id);return table->rowCount()==2&&selected>=0&&table->item(selected,7)->text()=="Completed";});require(select(window,id)>=0,"Unified history contains torrent");require(table->item(table->currentRow(),0)->data(Qt::UserRole+1).toString().startsWith("Torrent"),"Visible torrent type and upload information");require(window.current().entries.size()==1&&window.current().entries[0].id==http,"Torrent UI leaves HTTP queue separate");
            auto navigation=window.findChildren<QPushButton*>("navigation");navigation[4]->click();require(control<QLabel>(window,"totalMetric")->text()=="1","Torrent metrics exclude HTTP rows");require(table->isRowHidden(select(window,http)),"Torrents page hides HTTP rows");require(!table->isRowHidden(select(window,id)),"Torrents page shows torrent");control<QLineEdit>(window,"searchField")->setText("missing");require(table->isRowHidden(table->currentRow()),"Torrent search filters rows");control<QLineEdit>(window,"searchField")->clear();for(int mode=0;mode<3;++mode){control<QComboBox>(window,"viewDownloads")->setCurrentIndex(mode);require(select(window,id)>=0,"Unified sorting retains stable torrent ID");}
            for(const auto size:{QSize(1000,650),QSize(800,550)}){window.resize(size);QApplication::processEvents();require(table->horizontalScrollBar()->maximum()==0,"Torrent history fits supported window size");require(window.grab().save(QDir(shots).filePath(QString("torrents-%1.bmp").arg(size.width())),"BMP"),"Torrent page rendered");}window.resize(1000,650);
            stage="details and file configuration";select(window,id);control<QPushButton>(window,"detailsToggle")->click();auto* details=control<QDialog>(window,"torrentDetailsDialog");auto* tabs=control<QTabWidget>(*details,"torrentTabs");require(tabs->count()==5&&tabs->tabText(1)=="Files"&&tabs->tabText(2)=="Trackers"&&tabs->tabText(3)=="Peers","Torrent detail tabs");wait([&]{return control<QPushButton>(*details,"torrentApply")->isEnabled();});files=control<QTableWidget>(*details,"torrentFiles");qobject_cast<QComboBox*>(files->cellWidget(1,3))->setCurrentIndex(4);control<QDoubleSpinBox>(*details,"torrentTaskDownload")->setValue(64);control<QDoubleSpinBox>(*details,"torrentTaskUpload")->setValue(128);control<QSpinBox>(*details,"torrentTaskConnections")->setValue(5);control<QComboBox>(*details,"torrentSeedMode")->setCurrentIndex(3);control<QPushButton>(*details,"torrentApply")->click();wait([&]{const auto s=row(*engine,id);return s.files[1].priority==4&&s.limits.download==65536&&s.seeding.mode==SeedMode::Manual;});wait([&]{return control<QPushButton>(*details,"torrentApply")->isEnabled();});details->reject();wait([&]{return !window.findChild<QDialog*>("torrentDetailsDialog");});select(window,id);stage="expanded selected native transfer";last=std::chrono::steady_clock::now();measuring=true;control<QPushButton>(window,"startButton")->click();complete(engine->connectPeer(id,"127.0.0.1",seed.listen_port()));wait([&]{return row(*engine,id).state==State::Seeding;},30);measuring=false;require(digest(QDir(save).filePath("gui-payload/file1.bin"))==digest(QDir(root).filePath("seed/gui-payload/file1.bin")),"Changed GUI selection independent final SHA-256");
            stage="overview and settings screenshot export";showTorrentDetails(*engine,id,&window);details=control<QDialog>(window,"torrentDetailsDialog");wait([&]{return control<QLabel>(*details,"torrentOverview")->text().contains("Ratio");});require(details->grab().save(QDir(shots).filePath("torrent-overview.bmp"),"BMP"),"Overview rendered");control<QTabWidget>(*details,"torrentTabs")->setCurrentIndex(4);require(details->grab().save(QDir(shots).filePath("torrent-task-settings.bmp"),"BMP"),"Per torrent settings rendered");details->reject();wait([&]{return !window.findChild<QDialog*>("torrentDetailsDialog");});
            stage="global limits dialog";std::cout<<"Global torrent limits"<<std::endl;control<QPushButton>(window,"torrentSettingsButton")->click();auto* limits=control<QDialog>(window,"torrentSettingsDialog");wait([&]{return control<QPushButton>(*limits,"torrentSettingsSave")->isEnabled();});control<QDoubleSpinBox>(*limits,"torrentGlobalDownload")->setValue(128);control<QDoubleSpinBox>(*limits,"torrentGlobalUpload")->setValue(64);control<QSpinBox>(*limits,"torrentGlobalConnections")->setValue(10);control<QPushButton>(*limits,"torrentSettingsSave")->click();wait([&]{return !window.findChild<QDialog*>("torrentSettingsDialog");});require(complete(engine->sessionLimits()).download==131072&&complete(engine->sessionLimits()).upload==65536&&complete(engine->sessionLimits()).connections==10,"GUI global limits apply to torrent session");
            stage="pause and removal dialogs";std::cout<<"Pause, cancel and removal"<<std::endl;select(window,id);control<QPushButton>(window,"pauseButton")->click();wait([&]{return row(*engine,id).state==State::Paused;});select(window,id);wait([&]{return control<QPushButton>(window,"cancelButton")->isEnabled();});control<QPushButton>(window,"cancelButton")->click();wait([&]{return row(*engine,id).state==State::Cancelled;});wait([&]{return control<QPushButton>(window,"moreButton")->isEnabled();});require(QFileInfo::exists(QDir(save).filePath("gui-payload/file0.bin")),"GUI Cancel retains completed payload");std::cout<<"Cancel removal dialog"<<std::endl;removeViaMenu(window,id,0);require(complete(engine->snapshots()).size()==1,"Cancel removal keeps history");std::cout<<"Keep-files removal dialog"<<std::endl;removeViaMenu(window,id,1);wait([&]{return complete(engine->snapshots()).empty();});require(QFileInfo::exists(QDir(save).filePath("gui-payload/file0.bin")),"Confirmed remove-only retains payload");wait([&]{return table->rowCount()==1;});
            stage="magnet metadata preview";std::cout<<"Magnet metadata preview"<<std::endl;const auto magnet=QString::fromStdString(lt::make_magnet_uri(params));retained=inspect(window,*engine,magnet,QDir(root).filePath("magnet"),true);add=control<QDialog>(window,"addTorrentDialog");complete(engine->connectPeer(retained,"127.0.0.1",seed.listen_port()));wait([&]{return row(*engine,retained).metadata&&control<QTableWidget>(*add,"torrentFiles")->rowCount()==2;});require(row(*engine,retained).metadataOnly&&row(*engine,retained).downloadedBytes==0&&row(*engine,retained).state==State::Paused,"Magnet resolves metadata and pauses without payload");files=control<QTableWidget>(*add,"torrentFiles");qobject_cast<QComboBox*>(files->cellWidget(0,3))->setCurrentIndex(0);qobject_cast<QComboBox*>(files->cellWidget(1,3))->setCurrentIndex(7);control<QPushButton>(*add,"torrentConfirm")->click();wait([&]{return !window.findChild<QDialog*>("addTorrentDialog");});require(row(*engine,retained).state==State::Paused&&!row(*engine,retained).metadataOnly,"Confirmed magnet add-paused");
            stage="deletion fixture and confirmation";const auto copy=QDir(root).filePath("delete/other-payload");require(QDir().mkpath(copy),"Deletion fixture folder");require(QFile::copy(QDir(save).filePath("gui-payload/file0.bin"),QDir(copy).filePath("file0.bin")),"Deletion fixture payload");QFile unrelated(QDir(root).filePath("delete/unrelated.txt"));require(unrelated.open(QIODevice::WriteOnly),"Unrelated fixture");unrelated.write("keep");unrelated.close();
            const auto erase=complete(engine->addFile(other,QDir(root).filePath("delete"),{true}));wait([&]{return select(window,erase)>=0;});removeViaMenu(window,erase,2);wait([&]{return complete(engine->snapshots()).size()==1;});require(!QFileInfo::exists(QDir(copy).filePath("file0.bin"))&&QFileInfo::exists(unrelated.fileName()),"Explicit GUI deletion removes payload only");
            require(complete(queue.snapshot()).settings.json()==httpBefore.settings.json()&&complete(queue.snapshot()).entries.size()==1&&complete(queue.snapshot()).entries[0].json()==httpBefore.entries[0].json(),"HTTP task and settings preserved across torrent GUI operations");
            stage="asynchronous main-window close";window.close();wait([&]{return !window.isVisible();});require(engine->shutdown().wait_for(std::chrono::seconds(0))==std::future_status::ready,"Window closes after asynchronous torrent checkpoint");
        }
        stage="native engine teardown";engine.reset();engine=std::make_unique<TorrentEngine>(Options{false,true,store});
        {
            stage="restored window construction";MainWindow restored(queue);restored.attachTorrents(*engine);restored.show();wait([&]{return restored.ready()&&control<QTableWidget>(restored,"downloadTable")->rowCount()==2;});const auto value=row(*engine,retained);require(value.state==State::Paused&&value.files[0].priority==0&&value.files[1].priority==7,"GUI restart restores metadata and selected files paused");require(complete(engine->sessionLimits()).connections==10,"GUI global limits survive restart");restored.close();wait([&]{return !restored.isVisible();});
        }
        std::cout<<"Measured transfer heartbeat: "<<beats<<" ticks; longest gap="<<longest<<" seconds; unscoped gap="<<unscoped<<" seconds during "<<slowest.toStdString()<<std::endl;require(beats>50&&longest<1.0,"Qt heartbeat stays responsive during native commands");std::cout<<"Torrent GUI checks passed: "<<checks<<"; heartbeat="<<beats<<"; longest gap="<<longest<<" seconds"<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<"Torrent GUI failed: "<<error.what()<<std::endl;return 1;}
}
