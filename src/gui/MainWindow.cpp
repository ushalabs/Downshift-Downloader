#include "MainWindow.h"
#include "MediaDialog.h"
#include "TorrentDialogs.h"
#include <QCloseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QDesktopServices>
#include <QStandardPaths>
#include "Brand.h"
#include "Controls.h"
#include "Design.h"
#include "DownloadDelegate.h"
#include "core/DownloadQueue/Categories.h"
#include <QShortcut>
#include <QAction>
#include "core/DownloadQueue/VideoUrl.h"
#include <QInputDialog>
#include <QUrl>
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QResizeEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numeric>
namespace downshift {
namespace {
QString bytes(std::uint64_t n) {
    double value=static_cast<double>(n); const char* units[]={"B","KB","MB","GB","TB"}; int unit=0;
    while(value>=1024 && unit<4) {value/=1024; ++unit;} return QString("%1 %2").arg(value,0,'f',unit ? 1 : 0).arg(units[unit]);
}
QString duration(double seconds) { if(!std::isfinite(seconds) || seconds<0) return "—"; const auto n=static_cast<qint64>(seconds); if(n>=3600) return QString("%1h %2m").arg(n/3600).arg((n/60)%60); if(n>=60) return QString("%1m %2s").arg(n/60).arg(n%60); return QString("%1s").arg(n); }
}
void MainWindow::openVideo(QString address,QString title,QString folder) {
    if(youtubeHost(QUrl(address).host())) {
        const auto canonical=youtubeVideo(address);
        if(canonical.isEmpty()){message("Open a specific YouTube video and use its Download this video button, or paste its watch/shorts link.",true);return;}
        address=canonical;
    }
    const QUrl url(address);
    if(!url.isValid() || (url.scheme()!="https" && url.scheme()!="http") || url.host().isEmpty() || !url.userInfo().isEmpty() || address.size()>32768 || address.contains(QChar(0))){message("Enter an HTTP or HTTPS video address without a username or password.",true);return;}
    int count=0;for(auto* child:findChildren<QDialog*>(QString(),Qt::FindDirectChildrenOnly))if(auto* dialog=dynamic_cast<MediaDialog*>(child)){++count;if(dialog->property("mediaUrl").toString()==address){dialog->showNormal();dialog->raise();dialog->activateWindow();return;}}
    if(count>=4){message("Close an existing video window before opening another.",true);return;}
    auto* dialog=new MediaDialog(queue_,std::move(address),std::move(title),this,std::move(folder));dialog->show();dialog->raise();dialog->activateWindow();
}
QPushButton* makeButton(const QString& text,const char* name,QWidget* parent) { auto* b=new QPushButton(text,parent); b->setObjectName(name); b->setCursor(Qt::PointingHandCursor); b->setMinimumHeight(36); return b; }
MainWindow::MainWindow(QueueManager& queue,QWidget* parent):QMainWindow(parent),queue_(queue) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));QApplication::setFont(QFont("Segoe UI",10));QApplication::setWindowIcon(brandIcon());
    setObjectName("downshiftMainWindow"); setWindowTitle("Downshift"); setWindowIcon(brandIcon()); resize(1000,650); setMinimumSize(800,550);
    auto* central=new QWidget(this); setCentralWidget(central); auto* shell=new QHBoxLayout(central); shell->setContentsMargins(0,0,0,0); shell->setSpacing(0);
    sidebar_=new QWidget; sidebar_->setObjectName("sidebar");sidebar_->setMaximumWidth(190);auto* side=new QVBoxLayout(sidebar_);side->setContentsMargins(12,22,12,16);side->setSpacing(8);
    auto* branding=new QHBoxLayout;auto* mark=new QLabel;mark->setPixmap(brandIcon().pixmap(32,32));branding->addWidget(mark);identity_=new QLabel("Downshift");identity_->setObjectName("brand");branding->addWidget(identity_);branding->addStretch();side->addLayout(branding);side->addSpacing(24);
    const QStringList navigation={"All downloads","In progress","Completed","Needs attention"};
    for(int i=0;i<navigation.size();++i) { auto* b=makeButton(navigation[i],"navigation");b->setProperty("navigationTitle",navigation[i]);b->setAccessibleName(navigation[i]);b->setToolTip(navigation[i]);b->setIconSize(QSize(20,20));b->setCheckable(true);b->setAutoExclusive(true);b->setChecked(i==0); side->addWidget(b); connect(b,&QPushButton::clicked,this,[this,i]{page_=i;if(ready_)apply(current_);else filter();table_->scrollToTop();}); }
    side->addStretch(); auto* settings=makeButton("Settings","settingsButton"); side->addWidget(settings); connect(settings,&QPushButton::clicked,this,&MainWindow::openSettings);
    shell->addWidget(sidebar_);
    auto* content=new QWidget; auto* layout=new QVBoxLayout(content); layout->setContentsMargins(20,20,20,14); layout->setSpacing(14); shell->addWidget(content,1);
    auto* heading=new QHBoxLayout;auto* titles=new QVBoxLayout;auto* title=new QLabel("Downloads");title->setObjectName("heading");titles->addWidget(title);summary_=new QLabel("Loading saved downloads…");summary_->setObjectName("summary");titles->addWidget(summary_);heading->addLayout(titles,1);
    video_=makeButton("Download video","videoButton");video_->setAccessibleName("Download video");video_->setProperty("tertiary",true);heading->addWidget(video_);connect(video_,&QPushButton::clicked,this,[this]{bool accepted=false;const auto url=QInputDialog::getText(this,"Download video","Video page or public video link",QLineEdit::Normal,{},&accepted).trimmed();if(accepted && !url.isEmpty())openVideo(url,"Video");});
    add_=makeButton("Add download","addButton");add_->setProperty("primary",true);add_->setEnabled(false);video_->setEnabled(false);heading->addWidget(add_);connect(add_,&QPushButton::clicked,this,[this]{if(page_==4)openTorrent();else openAdd();});layout->addLayout(heading);
    auto* stats=new QHBoxLayout;stats->setSpacing(10);const auto metric=[&](const QString& title,const char* name){auto* card=new QWidget;card->setObjectName("summaryCard");auto* box=new QVBoxLayout(card);box->setContentsMargins(14,12,14,12);box->setSpacing(4);auto* caption=new QLabel(title);caption->setObjectName("muted");box->addWidget(caption);auto* value=new QLabel("—");value->setObjectName(name);value->setProperty("metric",true);box->addWidget(value);stats->addWidget(card,1);return value;};
    totalMetric_=metric("Total downloads","totalMetric");activeMetric_=metric("Active","activeMetric");speed_=metric("Total speed","aggregateSpeed");dataMetric_=metric("Data downloaded","dataMetric");layout->addLayout(stats);
    auto* toolbar=new QHBoxLayout; search_=new QLineEdit; search_->setObjectName("searchField"); search_->setPlaceholderText("Search files or sources"); search_->setClearButtonEnabled(true); toolbar->addWidget(search_,1);
    search_->setMinimumWidth(180);search_->setAccessibleName("Search downloads");search_->addAction(lineIcon(Symbol::Search),QLineEdit::LeadingPosition);search_->setPlaceholderText("Search downloads or sources…");
    view_=new QComboBox;view_->setObjectName("viewDownloads");view_->setAccessibleName("Sort downloads");view_->setToolTip("Sort downloads by time or size");view_->addItems({"Latest","Oldest","Size"});view_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);view_->setMinimumContentsLength(5);toolbar->addWidget(view_);
    start_=makeButton("Resume","startButton");pause_=makeButton("Pause","pauseButton");cancel_=makeButton("Cancel","cancelButton");cancel_->setProperty("destructive",true);more_=makeButton("More","moreButton");more_->setToolTip("Download actions; includes Resume, Pause and Cancel in compact windows");for(auto* b:{start_,pause_,cancel_,more_})toolbar->addWidget(b);layout->addLayout(toolbar);
    connect(search_,&QLineEdit::textChanged,this,[this]{filter();}); connect(view_,&QComboBox::currentIndexChanged,this,[this]{apply(current_);table_->scrollToTop();});
    modernCombo(view_);
    connect(start_,&QPushButton::clicked,this,[this]{perform(QueueAction::Start);}); connect(pause_,&QPushButton::clicked,this,[this]{perform(QueueAction::Pause);}); connect(cancel_,&QPushButton::clicked,this,[this]{perform(QueueAction::Cancel);}); connect(more_,&QPushButton::clicked,this,&MainWindow::more);
    banner_=new QLabel;banner_->setTextFormat(Qt::PlainText); banner_->setObjectName("banner"); banner_->setWordWrap(true); banner_->hide(); layout->addWidget(banner_);
    auto* list=new QWidget; auto* listLayout=new QVBoxLayout(list); listLayout->setContentsMargins(0,0,0,0);
    emptyPanel_=new QWidget;emptyPanel_->setObjectName("emptyPanel");auto* emptyLayout=new QVBoxLayout(emptyPanel_);emptyLayout->setAlignment(Qt::AlignCenter);emptyLayout->setSpacing(12);auto* illustration=new QLabel;illustration->setAlignment(Qt::AlignCenter);illustration->setPixmap(lineIcon(Symbol::Download,QColor("#e91e79")).pixmap(48,48));emptyLayout->addWidget(illustration);empty_=new QLabel("Loading saved downloads…");empty_->setObjectName("emptyState");empty_->setAlignment(Qt::AlignCenter);emptyLayout->addWidget(empty_);emptyHelp_=new QLabel("Your history is being opened.");emptyHelp_->setWordWrap(true);emptyHelp_->setAlignment(Qt::AlignCenter);emptyHelp_->setObjectName("muted");emptyLayout->addWidget(emptyHelp_);emptyAdd_=makeButton("Add download","emptyAdd");emptyAdd_->setProperty("primary",true);emptyAdd_->setEnabled(false);emptyLayout->addWidget(emptyAdd_,0,Qt::AlignCenter);connect(emptyAdd_,&QPushButton::clicked,this,[this]{if(page_==4)openTorrent();else openAdd();});listLayout->addWidget(emptyPanel_,1);
    table_=new QTableWidget(0,9);table_->setObjectName("downloadTable");table_->setHorizontalHeaderLabels({"File","Size","Downloaded","Progress","Speed","Time left","Connections","Status","Type / Priority"});table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setShowGrid(false);table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(64);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(const auto& pair:std::vector<std::pair<int,int>>{{1,88},{2,95},{3,124},{4,96},{5,82},{6,90},{7,120},{8,70}})table_->setColumnWidth(pair.first,pair.second);table_->setItemDelegate(new DownloadDelegate(table_));table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);table_->setContextMenuPolicy(Qt::CustomContextMenu);listLayout->addWidget(table_);table_->hide();layout->addWidget(list,1);
    connect(table_,&QTableWidget::itemSelectionChanged,this,&MainWindow::details); connect(table_,&QTableWidget::customContextMenuRequested,this,[this](const QPoint&){more();});
    retryHistory_=makeButton("Retry loading","retryHistory");retryHistory_->hide();emptyLayout->addWidget(retryHistory_,0,Qt::AlignCenter);connect(retryHistory_,&QPushButton::clicked,this,[this]{retryHistory_->setEnabled(false);empty_->setText("Loading saved downloads…");emptyHelp_->setText("Opening the existing database; download files are preserved.");watch(queue_.retryLoading(),[this]{banner_->hide();refresh();});});
    emptyHelp_->setMinimumHeight(64);emptyHelp_->setMaximumWidth(480);emptyHelp_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    detailsContainer_=new QWidget;auto* detailLayout=new QVBoxLayout(detailsContainer_);detailLayout->setContentsMargins(0,0,0,0);detailToggle_=makeButton("Download details","detailsToggle");detailToggle_->setCheckable(true);detailToggle_->setChecked(true);detailLayout->addWidget(detailToggle_);connect(detailToggle_,&QPushButton::toggled,this,[this](bool expanded){if(!selectedTorrent())detailScroll_->setVisible(expanded);});
    auto* detail=new QWidget;detail->setObjectName("detailsPanel");auto* dl=new QVBoxLayout(detail);dl->setContentsMargins(16,12,16,12);detailTitle_=new QLabel("Select a download");detailTitle_->setTextFormat(Qt::PlainText);detailTitle_->setObjectName("detailTitle");dl->addWidget(detailTitle_);
    auto* columns=new QHBoxLayout; auto* left=new QFormLayout; auto* right=new QFormLayout; columns->addLayout(left,1); columns->addSpacing(30); columns->addLayout(right,1); dl->addLayout(columns); dl->addStretch();
    const QStringList names={"Source","Destination","Size","Downloaded","Speed","Average speed","Elapsed","Time left","Connections","Verified bytes","Resume","Error"};
    for(int i=0;i<names.size();++i) { auto* label=new QLabel("—"); label->setObjectName("detail"+QString::number(i)); label->setTextFormat(Qt::PlainText); label->setTextInteractionFlags(Qt::TextSelectableByMouse); label->setWordWrap(true); label->setMaximumHeight(42); label->setMinimumWidth(0); (i<6 ? left : right)->addRow(names[i],label); detailLabels_.emplace_back(names[i],label); }
    detailScroll_=new QScrollArea;detailScroll_->setWidgetResizable(true);detailScroll_->setFrameShape(QFrame::NoFrame);detailScroll_->setWidget(detail);detailScroll_->setMaximumHeight(190);detailLayout->addWidget(detailScroll_);layout->addWidget(detailsContainer_);detailsContainer_->hide();
    connect(detailToggle_,&QPushButton::clicked,this,[this]{if(const auto* row=selectedTorrent())showTorrentDetails(*torrents_,row->id,this);});
    tray_=new QSystemTrayIcon(windowIcon(),this); tray_->setToolTip("Downshift"); if(QSystemTrayIcon::isSystemTrayAvailable()) tray_->show();
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::Trigger){showNormal();raise();activateWindow();}});
    statusBar()->showMessage("Your downloads stay on this computer."); theme(false); details();
    const auto shortcut=[&](const QKeySequence& keys,const std::function<void()>& action){auto* key=new QShortcut(keys,this);connect(key,&QShortcut::activated,this,action);};shortcut(QKeySequence("Ctrl+N"),[this]{if(page_==4)openTorrent();else openAdd();});shortcut(QKeySequence("Ctrl+F"),[this]{search_->setFocus();search_->selectAll();});shortcut(QKeySequence("Ctrl+,"),[this]{openSettings();});shortcut(QKeySequence("Escape"),[this]{table_->clearSelection();table_->setCurrentCell(-1,-1);details();});
    auto* timer=new QTimer(this); timer->setInterval(250); connect(timer,&QTimer::timeout,this,&MainWindow::refresh); timer->start(); refresh();
}
void MainWindow::refresh() {
    if(torrentClosing_)return;
    if(pending_) {
        if(pending_->wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        try { apply(pending_->get()); } catch(const std::exception& e) {message(QString::fromUtf8(e.what()),true);add_->setEnabled(false);video_->setEnabled(false);emptyAdd_->hide();start_->setEnabled(false);pause_->setEnabled(false);cancel_->setEnabled(false);if(!ready_){retryHistory_->show();retryHistory_->setEnabled(true);summary_->setText("History unavailable");empty_->setText("History could not be loaded");emptyHelp_->setText("Your history and files are preserved. Close any other Downshift window, then retry.");}}
        pending_.reset();
    }
    pending_.emplace(queue_.snapshot());
}
void MainWindow::apply(const QueueSnapshot& snapshot) {
    for(const auto& e:snapshot.entries)if(!e.handoffKey.isEmpty() && e.status==QueueStatus::Paused && e.error=="Choose where to save this download."){
        const auto old=std::find_if(current_.entries.begin(),current_.entries.end(),[&](const auto& row){return row.id==e.id;});
        if(old==current_.entries.end() || old->status==QueueStatus::Prepared)QTimer::singleShot(0,this,[this,e]{confirmDownload(e);});
    }
    if(!ready_ || current_.settings.dark!=snapshot.settings.dark) theme(snapshot.settings.dark);
    if(ready_ && snapshot.settings.notifications) for(const auto& e:snapshot.entries) if(e.status==QueueStatus::Completed) {
        const auto old=std::find_if(current_.entries.begin(),current_.entries.end(),[&](const auto& row){return row.id==e.id;});
        if(old!=current_.entries.end() && old->status!=QueueStatus::Completed) { statusBar()->showMessage("Download complete: "+QFileInfo(e.destination).fileName(),10000); if(tray_->isVisible()) tray_->showMessage("Download complete",QFileInfo(e.destination).fileName(),QSystemTrayIcon::Information,5000); }
    }
    const auto scrollPosition=table_->verticalScrollBar()->value();
    QString selection;bool torrentSelection=false; if(const auto* e=selected()) selection=e->id;else if(const auto* entry=selectedTorrent()){selection=entry->id;torrentSelection=true;}
    current_=snapshot;ready_=true;retryHistory_->hide();const QSignalBlocker blocked(table_);table_->setRowCount(static_cast<int>(snapshot.entries.size()+torrentRows_.size()));
    table_->clearSelection();table_->setCurrentCell(-1,-1);
    displayOrder_.clear();for(std::size_t n=0;n<current_.entries.size();++n)displayOrder_.push_back({false,n});for(std::size_t n=0;n<torrentRows_.size();++n)displayOrder_.push_back({true,n});
    std::sort(displayOrder_.begin(),displayOrder_.end(),[this](auto a,auto b){
        const auto size=[this](auto row)->quint64{return row.torrent?static_cast<quint64>(torrentRows_[row.index].selectedBytes):current_.entries[row.index].progress.totalBytes.value_or(0);};
        const auto created=[this](auto row){return row.torrent?torrentRows_[row.index].created:current_.entries[row.index].created;};
        if(view_->currentIndex()==2&&size(a)!=size(b))return size(a)>size(b);
        if(created(a)!=created(b))return view_->currentIndex()==1?created(a)<created(b):created(a)>created(b);
        if(a.torrent!=b.torrent)return a.torrent;
        return view_->currentIndex()==1?a.index<b.index:a.index>b.index;
    });
    int running=0,queued=0;double speed=0;std::uint64_t downloaded=0;table_->setUpdatesEnabled(false);
    for(int row=0;row<table_->rowCount();++row) {
        const auto display=displayOrder_[static_cast<std::size_t>(row)];
        if(display.torrent){const auto& entry=torrentRows_[display.index];renderTorrentRow(row,entry);if(torrentActive(entry)){++running;speed+=entry.downloadBytesPerSecond;}downloaded+=static_cast<std::uint64_t>(entry.verifiedBytes);if(torrentSelection&&entry.id==selection)table_->selectRow(row);continue;}
        const auto& e=current_.entries[display.index]; const auto& p=e.progress; const bool active=e.status==QueueStatus::Running;
        if(active){++running;speed+=p.smoothedBytesPerSecond;} if(e.status==QueueStatus::Queued)++queued;
        downloaded+=p.downloadedBytes;
        const auto percent=e.status==QueueStatus::Completed ? 100.0:p.totalBytes && *p.totalBytes ? std::min(e.media && active ? 99.0:100.0,100.0*static_cast<double>(p.downloadedBytes)/static_cast<double>(*p.totalBytes)) : 0;
        const QStringList text={e.media && e.status!=QueueStatus::Completed ? e.mediaTitle:QFileInfo(e.destination).fileName(),p.totalBytes ? bytes(*p.totalBytes):"—",bytes(p.downloadedBytes),"",active ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",active && p.etaSeconds ? duration(*p.etaSeconds):"—",active ? QString::number(p.activeRanges):"—",statusText(e.status),e.media ? "Video":e.priority>0 ? "High":e.priority<0 ? "Low":"Normal"};
        for(int col=0;col<9;++col) {
            auto* item=table_->item(row,col); if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);} if(item->text()!=text[col]) item->setText(text[col]);
            if(col==0){item->setData(Qt::UserRole+1,(e.media?QString("Video · "):QString("HTTP · "))+QUrl(e.url).host());item->setData(Qt::UserRole+10,false);const auto category=downloadCategory(QFileInfo(e.destination).fileName());item->setIcon(lineIcon(category=="Videos" ? Symbol::Video:category=="Audios" ? Symbol::Audio:category=="Zip" ? Symbol::Archive:Symbol::Program,QColor(category=="Videos" ? "#5286cb":category=="Audios" ? "#9871d7":category=="Zip" ? "#d69a3b":"#e91e79")));}
            if(col==3){item->setData(Qt::UserRole+1,percent);item->setData(Qt::UserRole+2,!p.totalBytes && e.status!=QueueStatus::Completed);item->setToolTip(bytes(p.downloadedBytes)+" downloaded");}
            if(col==7)item->setData(Qt::UserRole+1,static_cast<int>(e.status));
            if(col==0) { item->setData(Qt::UserRole,e.id); item->setToolTip(e.destination); }
            if(col==7) item->setForeground(QColor(e.status==QueueStatus::Failed ? "#ef4444":e.status==QueueStatus::Completed ? "#16a34a":current_.settings.dark ? "#cbd5e1":"#475569"));
        }
        if(!torrentSelection&&e.id==selection) table_->selectRow(row);
    }
    summary_->setText(QString("%1 active  ·  %2 queued  ·  %3 total").arg(running).arg(queued).arg(table_->rowCount()));totalMetric_->setText(QString::number(table_->rowCount()));activeMetric_->setText(QString::number(running));speed_->setText(bytes(static_cast<std::uint64_t>(speed))+"/s");dataMetric_->setText(bytes(downloaded));add_->setEnabled(true);video_->setEnabled(true);emptyAdd_->setEnabled(true);filter();details();table_->setUpdatesEnabled(true);if(!snapshot.error.isEmpty())message(snapshot.error,true);
    table_->verticalScrollBar()->setValue(scrollPosition);
}
const QueueEntry* MainWindow::selected() const {
    const auto row=table_->currentRow(); if(row<0 || !table_->item(row,0) || table_->isRowHidden(row)) return nullptr;
    if(table_->item(row,0)->data(Qt::UserRole+10).toBool())return nullptr;
    const auto id=table_->item(row,0)->data(Qt::UserRole).toString(); const auto i=std::find_if(current_.entries.begin(),current_.entries.end(),[&](const auto& e){return e.id==id;}); return i==current_.entries.end() ? nullptr : &*i;
}
void MainWindow::filter() {
    int navigationIndex=0;const Symbol symbols[]={Symbol::Download,Symbol::Activity,Symbol::Check,Symbol::Attention,Symbol::Download};for(auto* button:findChildren<QPushButton*>("navigation")){button->setChecked(navigationIndex==page_);button->setIcon(lineIcon(symbols[navigationIndex],QColor(navigationIndex==page_ ? "#e91e79":current_.settings.dark ? "#a8b1c2":"#697386")));++navigationIndex;}
    if(page_==4){int active=0,seeding=0;quint64 done=0,rate=0;for(const auto& row:torrentRows_){if(torrentActive(row))++active;if(row.state==torrent::State::Seeding)++seeding;done+=static_cast<quint64>(row.verifiedBytes);rate+=static_cast<quint64>(row.downloadBytesPerSecond);}summary_->setText(QString("%1 active · %2 seeding · %3 torrents").arg(active).arg(seeding).arg(torrentRows_.size()));totalMetric_->setText(QString::number(torrentRows_.size()));activeMetric_->setText(QString::number(active));speed_->setText(bytes(rate)+"/s");dataMetric_->setText(bytes(done));}
    int visible=0;
    for(int row=0;row<table_->rowCount();++row) {
        const auto display=displayOrder_[static_cast<std::size_t>(row)];const auto text=search_->text();bool status=false,match=false;
        if(display.torrent){const auto& e=torrentRows_[display.index];status=page_==0||page_==4||(page_==1&&(torrentActive(e)||e.state==torrent::State::Paused))||(page_==2&&e.state==torrent::State::Complete)||(page_==3&&(e.state==torrent::State::Failed||e.state==torrent::State::Cancelled));match=e.name.contains(text,Qt::CaseInsensitive)||e.directory.contains(text,Qt::CaseInsensitive);}
        else{const auto& e=current_.entries[display.index];status=page_==0||(page_==1&&(e.status==QueueStatus::Running||e.status==QueueStatus::Queued||e.status==QueueStatus::Paused))||(page_==2&&e.status==QueueStatus::Completed)||(page_==3&&(e.status==QueueStatus::Failed||e.status==QueueStatus::Cancelled));match=e.destination.contains(text,Qt::CaseInsensitive)||e.url.contains(text,Qt::CaseInsensitive);}
        table_->setRowHidden(row,!status||!match);
        if(!table_->isRowHidden(row))++visible;
    }if(ready_){table_->setVisible(visible>0);emptyPanel_->setVisible(visible==0);empty_->setText((page_==4?torrentRows_.empty():table_->rowCount()==0) ? (page_==4?"Add your first torrent":"Ready when you are"):"No matching downloads");emptyHelp_->setText((page_==4?torrentRows_.empty():table_->rowCount()==0) ? (page_==4 ? "Add a torrent file or magnet link.":"Add a link to start your first download."):"Try another search or choose a different section.");emptyAdd_->show();}add_->setText(page_==4?"Add torrent":"Add download");emptyAdd_->setText(page_==4?"Add torrent":"Add download");video_->setVisible(page_!=4);if(auto* settings=findChild<QPushButton*>("torrentSettingsButton"))settings->setVisible(page_==4);if(auto* heading=findChild<QLabel*>("heading"))heading->setText(page_==4?"Torrents":"Downloads");details();
}
void MainWindow::details() {
    if(const auto* row=selectedTorrent()){
        start_->setText(row->metadataOnly?(row->metadata?"Choose files":"Find metadata"):"Resume");start_->setEnabled(!torrentClosing_&&row->state!=torrent::State::Cancelled&&(row->metadataOnly||(row->state!=torrent::State::Seeding&&row->state!=torrent::State::Downloading)));
        pause_->setEnabled(!torrentClosing_&&torrentActive(*row));cancel_->setEnabled(!torrentClosing_&&row->state!=torrent::State::Cancelled);more_->setEnabled(!torrentClosing_);detailsContainer_->show();detailScroll_->hide();detailToggle_->setCheckable(false);detailToggle_->setText("Torrent details…");detailTitle_->setWordWrap(true);detailTitle_->setText(QString("%1 · %2 · Upload %3/s · Seeds %4 · Peers %5 · Ratio %6").arg(row->name,torrentStateText(*row),bytes(static_cast<std::uint64_t>(row->uploadBytesPerSecond))).arg(row->seeds).arg(row->peers).arg(row->ratio,0,'f',2));return;
    }
    detailToggle_->setCheckable(true);detailToggle_->setText("Download details");
    const auto* e=selected();start_->setText(e && (e->media || (!e->browserTransfer && !youtubeVideo(e->url).isEmpty())) ? "Retry video":"Resume"); start_->setEnabled(e && e->status!=QueueStatus::Completed && e->status!=QueueStatus::Running && e->status!=QueueStatus::Prepared); pause_->setEnabled(e && !e->media && (e->status==QueueStatus::Running || e->status==QueueStatus::Queued)); cancel_->setEnabled(e && (e->media ? e->status==QueueStatus::Running:e->status!=QueueStatus::Completed && e->status!=QueueStatus::Cancelled && e->status!=QueueStatus::Prepared)); more_->setEnabled(e!=nullptr);
    detailsContainer_->setVisible(e!=nullptr);if(!e) {detailTitle_->setText("Select a download");for(auto& [name,label]:detailLabels_)label->setText("—");return;}
    detailScroll_->setVisible(detailToggle_->isChecked());
    const auto& p=e->progress; detailTitle_->setText((e->media && e->status!=QueueStatus::Completed ? e->mediaTitle:QFileInfo(e->destination).fileName())+"  ·  "+statusText(e->status));
    const QStringList text={e->url,e->destination,p.totalBytes ? bytes(*p.totalBytes):"Unknown",bytes(p.downloadedBytes),e->status==QueueStatus::Running ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",bytes(static_cast<std::uint64_t>(p.averageBytesPerSecond))+"/s",duration(p.elapsedSeconds),p.etaSeconds ? duration(*p.etaSeconds):"—",QString("%1 active / %2 target").arg(p.activeRanges).arg(p.targetConnections),bytes(p.verifiedBytes),p.resumable ? "Supported; verified before reuse":"Requires server validation; some files need Restart",e->error.isEmpty() ? "None":e->error};
    for(std::size_t i=0;i<detailLabels_.size();++i){const auto value=e->media && i==10 ? QString("Use Retry video; partial streams are kept where the site permits."):text[static_cast<int>(i)];detailLabels_[i].second->setText(value);detailLabels_[i].second->setToolTip(value);}
}
void MainWindow::message(const QString& text,bool error) {banner_->setText(text);banner_->setProperty("error",error);banner_->show();banner_->style()->unpolish(banner_);banner_->style()->polish(banner_);statusBar()->showMessage(text,10000);}
void MainWindow::perform(QueueAction action) {
    if(const auto* row=selectedTorrent()){
        const auto id=row->id;if(action==QueueAction::Start){if(row->metadataOnly&&row->metadata){showTorrentDetails(*torrents_,id,this);return;}watch(torrents_->resume(id),[this]{banner_->hide();});}
        else if(action==QueueAction::Pause)watch(torrents_->pause(id),[this]{banner_->hide();});else if(action==QueueAction::Cancel)watch(torrents_->cancel(id),[this]{banner_->hide();});return;
    }
    if(const auto* e=selected()) {
    if(!e->media && !e->browserTransfer && !youtubeVideo(e->url).isEmpty() && action==QueueAction::Start){openVideo(e->url,"YouTube video");return;}
    if(e->media){if(action==QueueAction::Start)openVideo(e->url,e->mediaTitle,e->mediaFolder);else if(action==QueueAction::Cancel)for(auto* dialog:findChildren<QDialog*>("mediaDialog"))if(dialog->property("mediaUrl").toString()==e->url)if(auto* media=dynamic_cast<MediaDialog*>(dialog))media->stop();return;}
    watch(queue_.action(e->id,action),[this]{banner_->hide();});
}}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);const auto width=event->size().width();if(table_){for(int column:{2,5,8})table_->setColumnHidden(column,width<1350);table_->setColumnHidden(6,width<1100);}if(!sidebar_)return;const bool compact=width<950;sidebar_->setMaximumWidth(compact ? 72:190);sidebar_->setProperty("compact",compact);identity_->setVisible(!compact);for(auto* button:findChildren<QPushButton*>("navigation"))button->setText(compact ? QString():button->property("navigationTitle").toString());if(auto* settings=findChild<QPushButton*>("settingsButton")){settings->setText(compact ? QString():"Settings");settings->setAccessibleName("Settings");settings->setToolTip("Settings");}if(auto* settings=findChild<QPushButton*>("torrentSettingsButton")){settings->setText(compact ? QString():"Torrent limits");settings->setAccessibleName("Torrent limits");settings->setToolTip("Torrent limits");}for(auto* button:{start_,pause_,cancel_})button->setVisible(!compact);video_->setText(compact ? QString():"Download video");video_->setToolTip("Download a video");sidebar_->style()->unpolish(sidebar_);sidebar_->style()->polish(sidebar_);
}
void MainWindow::theme(bool dark) {
    applyDesign(dark);const QColor color(dark ? "#a8b1c2":"#697386");
    int index=0;const Symbol symbols[]={Symbol::Download,Symbol::Activity,Symbol::Check,Symbol::Attention,Symbol::Download};for(auto* button:findChildren<QPushButton*>("navigation")){button->setIcon(lineIcon(symbols[index++],button->isChecked() ? QColor("#e91e79"):color));}
    add_->setIcon(lineIcon(Symbol::Plus,Qt::white));emptyAdd_->setIcon(lineIcon(Symbol::Plus,Qt::white));video_->setIcon(lineIcon(Symbol::Video,color));start_->setIcon(lineIcon(Symbol::Resume,color));pause_->setIcon(lineIcon(Symbol::Pause,color));cancel_->setIcon(lineIcon(Symbol::Cancel,QColor("#dc4554")));more_->setIcon(lineIcon(Symbol::More,color));detailToggle_->setIcon(lineIcon(Symbol::Chevron,color));
    if(auto* button=findChild<QPushButton*>("settingsButton"))button->setIcon(lineIcon(Symbol::Settings,color));
    if(!search_->actions().isEmpty())search_->actions().first()->setIcon(lineIcon(Symbol::Search,color));
}
}
