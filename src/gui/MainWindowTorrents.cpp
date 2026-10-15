#include "MainWindow.h"
#include "TorrentDialogs.h"
#include "Design.h"
#include <QCloseEvent>
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
    auto* side=qobject_cast<QVBoxLayout*>(sidebar_->layout());auto* navigation=makeButton("Torrents","navigation",sidebar_);navigation->setProperty("navigationTitle","Torrents");navigation->setAccessibleName("Torrents");navigation->setToolTip("Torrents");navigation->setIconSize(QSize(20,20));navigation->setCheckable(true);navigation->setAutoExclusive(true);auto* badge=new QLabel("0",navigation);badge->setObjectName("navigationCount");badge->setAlignment(Qt::AlignCenter);badge->setFixedSize(22,22);badge->setAttribute(Qt::WA_TransparentForMouseEvents);auto* badgeLayout=new QHBoxLayout(navigation);badgeLayout->setContentsMargins(0,0,6,0);badgeLayout->addStretch();badgeLayout->addWidget(badge);side->insertWidget(side->count()-2,navigation);
    connect(navigation,&QPushButton::clicked,this,[this]{page_=4;if(ready_)apply(current_);else filter();table_->scrollToTop();});
    auto* settings=makeButton("Torrent limits","torrentSettingsButton",sidebar_);settings->hide();settings->setIcon(lineIcon(Symbol::Settings));side->insertWidget(side->count()-2,settings);connect(settings,&QPushButton::clicked,this,[this]{showTorrentSettings(*torrents_,this);});
    connect(&engine,&torrent::TorrentEngine::engineFailed,this,[this](const QString& error){message("Torrent history: "+error,true);},Qt::QueuedConnection);
    auto* timer=new QTimer(this);timer->setObjectName("torrentRefreshTimer");timer->setInterval(250);
    connect(timer,&QTimer::timeout,this,[this,timer]{
        if(torrentClosing_)return;
        if(torrentPending_){
            if(torrentPending_->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            try{torrentRows_=torrentPending_->get();if(ready_)apply(current_);}
            catch(const std::exception& error){message("Torrent history could not be loaded: "+QString::fromUtf8(error.what())+" Existing files were retained.",true);timer->stop();}
            torrentPending_.reset();
        }
        if(timer->isActive())torrentPending_.emplace(torrents_->snapshots());
    });timer->start();theme(current_.settings.dark);
}
void MainWindow::openTorrent(){if(!torrents_||torrentClosing_)return;const auto folder=current_.settings.directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::DownloadLocation):current_.settings.directory;showAddTorrent(*torrents_,folder,this);}
const torrent::Snapshot* MainWindow::selectedTorrent()const{
    const auto row=table_->currentRow();if(row<0||table_->isRowHidden(row)||!table_->item(row,0)||!table_->item(row,0)->data(Qt::UserRole+10).toBool())return nullptr;
    const auto id=table_->item(row,0)->data(Qt::UserRole).toString();for(const auto& value:torrentRows_)if(value.id==id)return &value;return nullptr;
}
void MainWindow::renderTorrentRow(int row,const torrent::Snapshot& s){
    const QStringList text={s.name.isEmpty()?QString("Magnet metadata"):s.name,s.metadata?amount(s.selectedBytes):QString("—"),amount(s.verifiedBytes),"",amount(s.downloadBytesPerSecond)+"/s","—",QString::number(s.peers),torrentStateText(s),"Torrent"};
    for(int col=0;col<11;++col){auto* item=table_->item(row,col);if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);}item->setText(col<9?text[col]:col==9?QDateTime::fromSecsSinceEpoch(s.created).toString("dd MMM"):QString());
        if(col==0){item->setData(Qt::UserRole,s.id);item->setData(Qt::UserRole+10,true);item->setData(Qt::UserRole+1,QString("Torrent · ↑ %1/s · %2 seeds · Ratio %3").arg(amount(s.uploadBytesPerSecond)).arg(s.seeds).arg(s.ratio,0,'f',2));item->setToolTip(s.directory);item->setIcon(lineIcon(Symbol::Torrent,QColor("#EC1680")));}
        if(col==3){item->setData(Qt::UserRole+3,static_cast<int>(s.state==torrent::State::Complete||s.state==torrent::State::Seeding?QueueStatus::Completed:s.state==torrent::State::Failed?QueueStatus::Failed:s.state==torrent::State::Cancelled?QueueStatus::Cancelled:torrentActive(s)?QueueStatus::Running:QueueStatus::Paused));item->setData(Qt::UserRole+1,s.selectedBytes?std::clamp(100.*static_cast<double>(s.verifiedBytes)/static_cast<double>(s.selectedBytes),0.,100.):0.);item->setData(Qt::UserRole+2,!s.metadata);item->setToolTip(amount(s.verifiedBytes)+" verified / "+amount(s.selectedBytes)+" selected");}
        if(col==7){const auto state=s.state==torrent::State::Complete?QueueStatus::Completed:s.state==torrent::State::Failed?QueueStatus::Failed:s.state==torrent::State::Cancelled?QueueStatus::Cancelled:torrentActive(s)?QueueStatus::Running:QueueStatus::Paused;item->setData(Qt::UserRole+1,static_cast<int>(state));}
    }
}
void MainWindow::torrentMenu(){
    const auto* selected=selectedTorrent();if(!selected)return;const auto row=*selected;auto* menu=new QMenu(this);menu->setObjectName("torrentContextMenu");connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);
    for(const auto& pair:std::vector<std::pair<QPushButton*,QueueAction>>{{start_,QueueAction::Start},{pause_,QueueAction::Pause},{cancel_,QueueAction::Cancel}}){auto* action=menu->addAction(pair.first->text(),[this,kind=pair.second]{perform(kind);});action->setEnabled(pair.first->isEnabled());}
    menu->addSeparator();menu->addAction("Overview, files and settings…",[this,row]{showTorrentDetails(*torrents_,row.id,this);});
    menu->addAction("Open destination folder",[this,row]{if(!QDesktopServices::openUrl(QUrl::fromLocalFile(row.directory)))message("Windows could not open this folder.",true);});
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
    if(!torrents_||torrentClosed_){QMainWindow::closeEvent(event);return;}
    event->ignore();if(torrentClosing_)return;torrentClosing_=true;setEnabled(false);message("Saving torrent progress…");if(auto* refresh=findChild<QTimer*>("torrentRefreshTimer"))refresh->stop();
    const auto pending=torrents_->shutdown();auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();torrentClosed_=true;try{pending.get();close();}catch(const std::exception& error){setEnabled(true);message("Torrent checkpoint failed: "+QString::fromUtf8(error.what())+" Previous recovery data and files were kept. Close Downshift to restart.",true);}});timer->start();
}
}
