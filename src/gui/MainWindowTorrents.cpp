#include "MainWindow.h"
#include "TorrentDialogs.h"
#include "MediaDialog.h"
#include "Design.h"
#include <QCloseEvent>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
namespace downshift {
namespace {
QString amount(qint64 n){double value=static_cast<double>(n);int unit=0;const QStringList units={"B","KiB","MiB","GiB","TiB"};while(value>=1024&&unit<4){value/=1024;++unit;}return QString::number(value,'f',unit?1:0)+" "+units[unit];}
}
void MainWindow::attachTorrents(torrent::TorrentEngine& engine){
    if(torrents_)return;torrents_=&engine;
    auto* side=qobject_cast<QVBoxLayout*>(sidebar_->layout());
    categoryLayout_->addWidget(navigationButton("Torrents",4,Symbol::Torrent));
    auto* settings=makeButton(sidebar_->property("compact").toBool()?QString():QString("Torrent limits"),"torrentSettingsButton",sidebar_);
    settings->setProperty("railCommand",true);settings->setAccessibleName("Torrent limits");settings->setToolTip("Torrent limits");
    settings->setIconSize(QSize(16,16));settings->setIcon(lineIcon(Symbol::Settings,DesignTokens::theme(current_.settings.dark).muted));settings->hide();
    side->insertWidget(side->count()-2,settings);connect(settings,&QPushButton::clicked,this,[this]{showTorrentSettings(*torrents_,this);});
    connect(&engine,&torrent::TorrentEngine::engineFailed,this,[this](const QString& error){message("Torrent history: "+error,true);},Qt::QueuedConnection);
    auto* timer=new QTimer(this);timer->setObjectName("torrentRefreshTimer");timer->setInterval(250);
    connect(timer,&QTimer::timeout,this,[this,timer]{
        if(torrentClosing_)return;
        if(torrentPending_){
            if(torrentPending_->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            try{torrentRows_=torrentPending_->get();for(auto i=torrentPanels_.begin();i!=torrentPanels_.end();){if(std::none_of(torrentRows_.begin(),torrentRows_.end(),[&](const auto& row){return row.id==i->first;})){i->second->deleteLater();i=torrentPanels_.erase(i);}else ++i;}if(ready_)apply(current_);}
            catch(const std::exception& error){message("Torrent history could not be loaded: "+QString::fromUtf8(error.what())+" Existing files were retained.",true);timer->stop();}
            torrentPending_.reset();
        }
        if(timer->isActive())torrentPending_.emplace(torrents_->snapshots());
    });timer->start();theme(current_.settings.dark);
}
void MainWindow::openTorrent(QString source){if(!torrents_||torrentClosing_)return;const auto folder=current_.settings.directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::DownloadLocation):current_.settings.directory;showAddTorrent(*torrents_,folder,this,std::move(source));}
const torrent::Snapshot* MainWindow::selectedTorrent()const{
    const auto row=activeSelectedRow();if(row<0||!table_->item(row,0)||!table_->item(row,0)->data(Qt::UserRole+10).toBool())return nullptr;
    const auto id=table_->item(row,0)->data(Qt::UserRole).toString();for(const auto& value:torrentRows_)if(value.id==id)return &value;return nullptr;
}
void MainWindow::renderTorrentRow(int row,const torrent::Snapshot& s){
    const auto eta=s.state==torrent::State::Downloading&&s.downloadBytesPerSecond>0&&s.selectedBytes>s.verifiedBytes
        ?transferDuration(static_cast<double>(s.selectedBytes-s.verifiedBytes)/s.downloadBytesPerSecond):QString("—");
    const QStringList text={s.name.isEmpty()?QString("Magnet metadata"):s.name,s.metadata?amount(s.selectedBytes):QString("—"),amount(s.verifiedBytes),"",amount(s.downloadBytesPerSecond)+"/s",eta,QString::number(s.peers),torrentStateText(s),"Torrent"};
    for(int col=0;col<11;++col){auto* item=table_->item(row,col);if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);}item->setText(col<9?text[col]:col==9?QDateTime::fromSecsSinceEpoch(s.created).toString("dd MMM"):QString());
        if(col==0){item->setData(Qt::UserRole,s.id);item->setData(Qt::UserRole+10,true);item->setData(Qt::UserRole+1,QString("Torrent · ↑ %1/s · %2 seeds · Ratio %3").arg(amount(s.uploadBytesPerSecond)).arg(s.seeds).arg(s.ratio,0,'f',2));item->setToolTip(QString("<qt>%1<br>%2</qt>").arg(text[0].toHtmlEscaped(),s.directory.toHtmlEscaped()));item->setData(Qt::AccessibleDescriptionRole,item->data(Qt::UserRole+1));item->setIcon(lineIcon(Symbol::Torrent,fileIconColor(Symbol::Torrent,current_.settings.dark)));}
        if(col==3){item->setData(Qt::UserRole+3,static_cast<int>(s.state==torrent::State::Complete||s.state==torrent::State::Seeding?QueueStatus::Completed:s.state==torrent::State::Failed?QueueStatus::Failed:s.state==torrent::State::Cancelled?QueueStatus::Cancelled:torrentActive(s)?QueueStatus::Running:QueueStatus::Paused));item->setData(Qt::UserRole+1,s.state==torrent::State::Complete||s.state==torrent::State::Seeding?100.:s.selectedBytes?std::clamp(100.*static_cast<double>(s.verifiedBytes)/static_cast<double>(s.selectedBytes),0.,100.):0.);item->setData(Qt::UserRole+2,!s.metadata||s.verifyingCompletion);item->setToolTip(amount(s.verifiedBytes)+" verified / "+amount(s.selectedBytes)+" selected");item->setData(Qt::AccessibleTextRole,item->data(Qt::UserRole+2).toBool()?QString("Progress unavailable"):QString::number(item->data(Qt::UserRole+1).toDouble(),'f',0)+"% complete");}
        if(col==7){const auto state=s.state==torrent::State::Complete?QueueStatus::Completed:s.state==torrent::State::Failed?QueueStatus::Failed:s.state==torrent::State::Cancelled?QueueStatus::Cancelled:torrentActive(s)?QueueStatus::Running:QueueStatus::Paused;item->setData(Qt::UserRole+1,static_cast<int>(state));}
    }
}
void MainWindow::torrentDetails(const QString& id){
    auto& panel=torrentPanels_[id];if(!panel){panel=createTorrentDetails(*torrents_,id,torrentDetailsStack_);torrentDetailsStack_->addWidget(panel);}
    detailTabs_->hide();torrentDetailsStack_->setCurrentWidget(panel);torrentDetailsStack_->show();
}
void MainWindow::torrentMenu(){
    const auto* selected=selectedTorrent();if(!selected)return;const auto row=*selected;auto* menu=new QMenu(this);menu->setObjectName("torrentContextMenu");connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);
    const auto color=DesignTokens::theme(current_.settings.dark).muted;
    if(row.state!=torrent::State::Complete)for(const auto& pair:std::vector<std::pair<QPushButton*,QueueAction>>{{start_,QueueAction::Start},{pause_,QueueAction::Pause},{cancel_,QueueAction::Cancel}}){if(pair.second==QueueAction::Start&&row.state==torrent::State::Seeding)continue;auto* action=menu->addAction(pair.first->icon(),pair.first->text(),[this,kind=pair.second]{perform(kind);});action->setEnabled(pair.first->isEnabled());}
    menu->addSeparator();menu->addAction(lineIcon(Symbol::Info,color),"Details",[this]{table_->setProperty("detailsAllowed",true);detailToggle_->setChecked(true);details();});
    menu->addAction(lineIcon(Symbol::Folder,color),"Open destination folder",[this,row]{if(!QDesktopServices::openUrl(QUrl::fromLocalFile(row.directory)))message("Windows could not open this folder.",true);});
    menu->addAction("Remove torrent…",[this,row,menu]{
        menu->close();
        // Let the popup finish closing before creating another native window.
        QTimer::singleShot(0,this,[this,row]{
            auto* confirm=new QMessageBox(QMessageBox::Question,"Remove torrent","Remove this torrent from history?\n\n"+row.name,QMessageBox::NoButton,this);confirm->setAttribute(Qt::WA_DeleteOnClose);confirm->setObjectName("torrentRemoveConfirmation");auto* keep=confirm->addButton("Remove; keep completed files",QMessageBox::AcceptRole);keep->setObjectName("torrentRemoveKeep");QPushButton* erase=nullptr;
            if(row.metadata){erase=confirm->addButton("Delete payload and remove",QMessageBox::DestructiveRole);erase->setObjectName("torrentRemoveDelete");}
            auto* cancel=confirm->addButton(QMessageBox::Cancel);confirm->setDefaultButton(cancel);confirm->setEscapeButton(cancel);
            connect(confirm,&QMessageBox::finished,this,[this,confirm,keep,erase]{
                if(confirm->clickedButton()==keep)watch(torrents_->remove(confirm->property("torrentId").toString()),[this]{message("Torrent removed. Completed files were kept.");});
                else if(erase&&confirm->clickedButton()==erase)watch(torrents_->remove(confirm->property("torrentId").toString(),true,true),[this]{message("Torrent payload deleted and history removed.");});
            });
            confirm->setProperty("torrentId",row.id);confirm->open();
        });
    });menu->popup(QCursor::pos());
}
void MainWindow::closeEvent(QCloseEvent* event){
    if(!exitRequested_&&ready_&&current_.settings.closeToTray&&QSystemTrayIcon::isSystemTrayAvailable()){
        event->ignore();for(auto* child:findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(child->isWindow()&&child->isVisible()){if(auto* media=dynamic_cast<MediaDialog*>(child))media->close();else {child->setProperty("showFromTray",true);child->hide();}}hide();return;
    }
    if(!exitRequested_){event->ignore();QTimer::singleShot(0,this,&MainWindow::requestExit);return;}
    if(!torrents_||torrentClosed_){QApplication::setQuitOnLastWindowClosed(true);QMainWindow::closeEvent(event);QTimer::singleShot(0,qApp,&QApplication::quit);return;}
    event->ignore();if(torrentClosing_)return;torrentClosing_=true;setEnabled(false);message("Saving torrent progress…");if(auto* refresh=findChild<QTimer*>("torrentRefreshTimer"))refresh->stop();
    const auto pending=torrents_->shutdown();auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();torrentClosed_=true;try{pending.get();close();}catch(const std::exception& error){setEnabled(true);message("Torrent checkpoint failed: "+QString::fromUtf8(error.what())+" Previous recovery data and files were kept. Close Downshift to restart.",true);}});timer->start();
}
}
