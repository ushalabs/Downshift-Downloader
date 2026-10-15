#include "MainWindow.h"
#include "MediaDialog.h"
#include "TorrentDialogs.h"
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QMimeData>
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
#include <QDateTime>
#include <QTabWidget>
#include <QClipboard>
#include <QGridLayout>
#include <QNetworkAccessManager>
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
    double value=static_cast<double>(n); const char* units[]={"B","KiB","MiB","GiB","TiB"}; int unit=0;
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
MainWindow::MainWindow(QueueManager& queue,QWidget* parent,QNetworkAccessManager* previewNetwork):QMainWindow(parent),queue_(queue) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));QApplication::setFont(QFont("Segoe UI",10));QApplication::setWindowIcon(brandIcon());
    setAcceptDrops(true);setObjectName("downshiftMainWindow"); setWindowTitle("Downshift"); setWindowIcon(brandIcon()); resize(1075,730); setMinimumSize(800,550);
    auto* central=new QWidget(this); setCentralWidget(central); auto* shell=new QHBoxLayout(central); shell->setContentsMargins(0,0,0,0); shell->setSpacing(0);
    sidebar_=new QWidget; sidebar_->setObjectName("sidebar");sidebar_->setMaximumWidth(224);auto* side=new QVBoxLayout(sidebar_);side->setContentsMargins(12,22,12,16);side->setSpacing(8);
    auto* branding=new QHBoxLayout;auto* mark=new QLabel;mark->setPixmap(brandIcon().pixmap(32,32));branding->addWidget(mark);identity_=new QLabel("Downshift");identity_->setObjectName("brand");branding->addWidget(identity_);branding->addStretch();side->addLayout(branding);side->addSpacing(18);
    const QStringList navigation={"All downloads","In progress","Completed","Needs attention"};
    for(int i=0;i<navigation.size();++i) { auto* b=makeButton(navigation[i],"navigation");b->setProperty("navigationTitle",navigation[i]);b->setAccessibleName(navigation[i]);b->setToolTip(navigation[i]);b->setIconSize(QSize(20,20));b->setCheckable(true);b->setAutoExclusive(true);b->setChecked(i==0); auto* count=new QLabel("0",b);count->setObjectName("navigationCount");count->setAlignment(Qt::AlignCenter);count->setFixedSize(22,22);count->setAttribute(Qt::WA_TransparentForMouseEvents);auto* badgeLayout=new QHBoxLayout(b);badgeLayout->setContentsMargins(0,0,6,0);badgeLayout->addStretch();badgeLayout->addWidget(count);side->addWidget(b); connect(b,&QPushButton::clicked,this,[this,i]{page_=i;if(ready_)apply(current_);else filter();table_->scrollToTop();}); }
    side->addStretch(); auto* settings=makeButton("Settings","settingsButton"); side->addWidget(settings); connect(settings,&QPushButton::clicked,this,&MainWindow::openSettings);
    shell->addWidget(sidebar_);
    auto* content=new QWidget; auto* layout=new QVBoxLayout(content); layout->setContentsMargins(20,20,20,14); layout->setSpacing(14); shell->addWidget(content,1);
    auto* heading=new QHBoxLayout;auto* titles=new QVBoxLayout;auto* title=new QLabel("Downloads");title->setObjectName("heading");titles->addWidget(title);summary_=new QLabel("Loading saved downloads…",this);summary_->setObjectName("summary");summary_->hide();heading->addLayout(titles,1);
    video_=makeButton("Download video","videoButton",this);video_->hide();
    add_=makeButton("Add download","addButton");add_->setProperty("primary",true);add_->setEnabled(false);video_->setEnabled(false);heading->addWidget(add_);connect(add_,&QPushButton::clicked,this,[this]{if(page_==4)openTorrent();else openAdd();});layout->addLayout(heading);
    auto* stats=new QHBoxLayout;stats->setSpacing(10);const auto metric=[&](const QString& title,const char* name,Symbol symbol,const QColor& color){
        auto* card=new QWidget;card->setObjectName("summaryCard");auto* box=new QHBoxLayout(card);box->setContentsMargins(12,14,12,14);box->setSpacing(12);
        auto* icon=new QLabel;icon->setObjectName("metricIcon");icon->setPixmap(lineIcon(symbol,color).pixmap(28,28));icon->setFixedSize(48,48);icon->setAlignment(Qt::AlignCenter);QColor tint=color;tint.setAlpha(22);icon->setStyleSheet(QString("background:%1;border-radius:24px;").arg(tint.name(QColor::HexArgb)));box->addWidget(icon);
        auto* labels=new QVBoxLayout;labels->setSpacing(4);auto* caption=new QLabel(title);caption->setObjectName("muted");labels->addWidget(caption);auto* value=new QLabel("—");value->setObjectName(name);value->setProperty("metric",true);labels->addWidget(value);box->addLayout(labels,1);stats->addWidget(card,1);return value;
    };
    totalMetric_=metric("Total downloads","totalMetric",Symbol::Download,QColor("#EC1680"));activeMetric_=metric("Active","activeMetric",Symbol::Activity,QColor("#347CFF"));speed_=metric("Total speed","aggregateSpeed",Symbol::Gauge,QColor("#13AA89"));dataMetric_=metric("Data downloaded","dataMetric",Symbol::Database,QColor("#8651ED"));dataMetric_->setToolTip("Bytes currently retained across tasks, including paused partials; not a lifetime traffic counter.");layout->addLayout(stats);
    auto* toolbar=new QHBoxLayout; search_=new QLineEdit; search_->setObjectName("searchField"); search_->setPlaceholderText("Search files or sources"); search_->setClearButtonEnabled(true); toolbar->addWidget(search_,1);
    search_->setMinimumWidth(180);search_->setAccessibleName("Search downloads");search_->addAction(lineIcon(Symbol::Search),QLineEdit::LeadingPosition);search_->setPlaceholderText("Search downloads or sources…");
    view_=new QComboBox;view_->setObjectName("viewDownloads");view_->setAccessibleName("Sort downloads");view_->setToolTip("Sort downloads by time or size");view_->addItems({"Latest","Oldest","Size"});view_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);view_->setMinimumContentsLength(5);toolbar->addWidget(view_);
    start_=makeButton("Resume","startButton");pause_=makeButton("Pause","pauseButton");cancel_=makeButton("Cancel","cancelButton");cancel_->setProperty("destructive",true);more_=makeButton("","moreButton");more_->setAccessibleName("More download actions");more_->setFixedWidth(42);more_->setToolTip("Download actions; includes Resume, Pause and Cancel in compact windows");for(auto* b:{start_,pause_,cancel_,more_})toolbar->addWidget(b);layout->addLayout(toolbar);
    connect(search_,&QLineEdit::textChanged,this,[this]{filter();}); connect(view_,&QComboBox::currentIndexChanged,this,[this]{apply(current_);table_->scrollToTop();});
    modernCombo(view_);
    connect(start_,&QPushButton::clicked,this,[this]{perform(QueueAction::Start);}); connect(pause_,&QPushButton::clicked,this,[this]{perform(QueueAction::Pause);}); connect(cancel_,&QPushButton::clicked,this,[this]{perform(QueueAction::Cancel);}); connect(more_,&QPushButton::clicked,this,&MainWindow::more);
    banner_=new Notification(content);
    auto* list=new QWidget; auto* listLayout=new QVBoxLayout(list); listLayout->setContentsMargins(0,0,0,0);
    emptyPanel_=new QWidget;emptyPanel_->setObjectName("emptyPanel");auto* emptyLayout=new QVBoxLayout(emptyPanel_);emptyLayout->setAlignment(Qt::AlignCenter);emptyLayout->setSpacing(12);auto* illustration=new QLabel;illustration->setAlignment(Qt::AlignCenter);illustration->setPixmap(lineIcon(Symbol::Download,QColor("#e91e79")).pixmap(48,48));emptyLayout->addWidget(illustration);empty_=new QLabel("Loading saved downloads…");empty_->setObjectName("emptyState");empty_->setAlignment(Qt::AlignCenter);emptyLayout->addWidget(empty_);emptyHelp_=new QLabel("Your history is being opened.");emptyHelp_->setWordWrap(true);emptyHelp_->setAlignment(Qt::AlignCenter);emptyHelp_->setObjectName("muted");emptyLayout->addWidget(emptyHelp_);emptyAdd_=makeButton("Add download","emptyAdd");emptyAdd_->setProperty("primary",true);emptyAdd_->setEnabled(false);emptyLayout->addWidget(emptyAdd_,0,Qt::AlignCenter);connect(emptyAdd_,&QPushButton::clicked,this,[this]{if(page_==4)openTorrent();else openAdd();});listLayout->addWidget(emptyPanel_,1);
    table_=new QTableWidget(0,11);table_->setObjectName("downloadTable");table_->setHorizontalHeaderLabels({"Name","Size","Downloaded","Progress","Speed","Time left","Connections","Status","Type / Priority","Added",""});table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setShowGrid(false);table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(64);table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(const auto& pair:std::vector<std::pair<int,int>>{{1,86},{2,95},{3,122},{4,100},{5,82},{6,90},{7,114},{8,70},{9,92},{10,34}})table_->setColumnWidth(pair.first,pair.second);table_->setItemDelegate(new DownloadDelegate(table_));table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);table_->setContextMenuPolicy(Qt::CustomContextMenu);listLayout->addWidget(table_);table_->hide();layout->addWidget(list,1);
    table_->setMouseTracking(true);table_->setColumnHidden(2,true);table_->setColumnHidden(5,true);table_->setColumnHidden(6,true);table_->setColumnHidden(8,true);
    connect(table_,&QTableWidget::cellClicked,this,[this](int,int col){if(col==10)more();});
    connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int,int){if(const auto* e=selected();e&&e->status==QueueStatus::Completed){const auto file=e->destination;auto* confirm=new QMessageBox(QMessageBox::Question,"Open downloaded file","Open this file with its Windows application?\n\n"+QFileInfo(file).fileName(),QMessageBox::Yes|QMessageBox::No,this);confirm->setAttribute(Qt::WA_DeleteOnClose);confirm->setDefaultButton(QMessageBox::No);connect(confirm,&QMessageBox::finished,this,[this,file](int answer){if(answer==QMessageBox::Yes&&!QDesktopServices::openUrl(QUrl::fromLocalFile(file)))message("Windows could not open this file.",true);});confirm->open();}});
    connect(table_,&QTableWidget::itemSelectionChanged,this,&MainWindow::details); connect(table_,&QTableWidget::customContextMenuRequested,this,[this](const QPoint&){more();});
    retryHistory_=makeButton("Retry loading","retryHistory");retryHistory_->hide();emptyLayout->addWidget(retryHistory_,0,Qt::AlignCenter);connect(retryHistory_,&QPushButton::clicked,this,[this]{retryHistory_->setEnabled(false);empty_->setText("Loading saved downloads…");emptyHelp_->setText("Opening the existing database; download files are preserved.");watch(queue_.retryLoading(),[this]{banner_->hide();refresh();});});
    emptyHelp_->setMinimumHeight(64);emptyHelp_->setMaximumWidth(480);emptyHelp_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    detailsContainer_=new QWidget;detailsContainer_->setObjectName("detailsPanel");auto* detailLayout=new QVBoxLayout(detailsContainer_);detailLayout->setContentsMargins(14,10,14,10);detailLayout->setSpacing(6);
    auto* detailHeader=new QHBoxLayout;detailHeader->setSpacing(14);thumbnail_=new QLabel;thumbnail_->setObjectName("previewFallback");thumbnail_->setFixedSize(128,72);thumbnail_->setAlignment(Qt::AlignCenter);detailHeader->addWidget(thumbnail_);thumbnail_->hide();
    auto* titleBox=new QVBoxLayout;titleBox->setSpacing(2);detailTitle_=new QLabel("Select a download");detailTitle_->setObjectName("detailTitle");detailTitle_->setTextFormat(Qt::PlainText);detailTitle_->setMinimumWidth(0);titleBox->addWidget(detailTitle_);
    sourceLink_=makeButton("","sourceLink");sourceLink_->setMinimumHeight(18);sourceLink_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);sourceLink_->setIcon(lineIcon(Symbol::Link,QColor("#EC1680")));titleBox->addWidget(sourceLink_);videoInfo_=new QLabel;videoInfo_->setObjectName("videoMetadata");videoInfo_->setTextFormat(Qt::PlainText);videoInfo_->setObjectName("muted");titleBox->addWidget(videoInfo_);detailHeader->addLayout(titleBox,1);
    detailAction_=makeButton("","detailAction");detailAction_->setFixedWidth(36);detailAction_->setToolTip("Pause or resume selected download");detailHeader->addWidget(detailAction_);connect(detailAction_,&QPushButton::clicked,this,[this]{if(const auto* e=selected();e&&e->status==QueueStatus::Completed){emit table_->cellDoubleClicked(table_->currentRow(),0);return;}perform(pause_->isEnabled()?QueueAction::Pause:QueueAction::Start);});
    detailFolder_=makeButton("","detailFolder");detailFolder_->setFixedWidth(36);detailFolder_->setIcon(lineIcon(Symbol::Folder));detailFolder_->setToolTip("Open destination folder");detailHeader->addWidget(detailFolder_);connect(detailFolder_,&QPushButton::clicked,this,[this]{QString folder;if(const auto* e=selected())folder=e->media&&e->status!=QueueStatus::Completed?e->mediaFolder:QFileInfo(e->destination).absolutePath();else if(const auto* row=selectedTorrent())folder=row->directory;if(!folder.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(folder));});
    detailCopy_=makeButton("","detailCopy");detailCopy_->setFixedWidth(36);detailCopy_->setIcon(lineIcon(Symbol::Copy));detailCopy_->setToolTip("Copy source link");detailHeader->addWidget(detailCopy_);connect(detailCopy_,&QPushButton::clicked,this,[this]{if(const auto* e=selected())QApplication::clipboard()->setText(e->videoMetadata.value("originalPage").toString(e->url));});
    detailToggle_=makeButton("","detailsToggle");detailToggle_->setAccessibleName("Expand or collapse download details");detailToggle_->setToolTip("Expand or collapse download details");detailToggle_->setFixedWidth(36);detailToggle_->setCheckable(true);detailToggle_->setChecked(true);detailHeader->addWidget(detailToggle_);auto* closeDetails=makeButton("","closeDetails");closeDetails->setFixedWidth(36);closeDetails->setIcon(lineIcon(Symbol::Cancel));closeDetails->setAccessibleName("Close download details");closeDetails->setToolTip("Close details");detailHeader->addWidget(closeDetails);connect(closeDetails,&QPushButton::clicked,this,[this]{table_->clearSelection();table_->setCurrentCell(-1,-1);details();});detailLayout->addLayout(detailHeader);
    auto* detail=new QWidget;auto* dl=new QVBoxLayout(detail);dl->setContentsMargins(0,0,0,0);detailTabs_=new QTabWidget;detailTabs_->setObjectName("downloadDetailsTabs");dl->addWidget(detailTabs_);auto* overview=new QWidget;auto* grid=new QGridLayout(overview);grid->setContentsMargins(2,6,2,2);grid->setHorizontalSpacing(16);grid->setVerticalSpacing(3);
    const QStringList names={"Source","Destination","Size","Downloaded","Speed","Average speed","Elapsed","Time left","Connections","Verified bytes","Resume","Error"};
    for(int i=0;i<names.size();++i){auto* label=new QLabel("—");label->setObjectName("detail"+QString::number(i));label->setTextFormat(Qt::PlainText);label->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);label->setMinimumWidth(0);label->setWordWrap(true);label->setMaximumHeight(36);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);auto* caption=new QLabel(names[i]);caption->setObjectName("muted");const int column=i%4,row=(i/4)*2;grid->addWidget(caption,row,column);grid->addWidget(label,row+1,column);grid->setColumnStretch(column,1);detailLabels_.emplace_back(names[i],label);}
    detailTabs_->addTab(overview,"Overview");connectionInfo_=new QLabel;connectionInfo_->setWordWrap(true);connectionInfo_->setMargin(10);detailTabs_->addTab(connectionInfo_,"Connections");taskLog_=new QLabel;taskLog_->setTextFormat(Qt::PlainText);taskLog_->setWordWrap(true);taskLog_->setMargin(10);detailTabs_->addTab(taskLog_,"Task status");
    detailScroll_=new QScrollArea;detailScroll_->setWidgetResizable(true);detailScroll_->setFrameShape(QFrame::NoFrame);detailScroll_->setWidget(detail);detailScroll_->setMaximumHeight(168);detailLayout->addWidget(detailScroll_);layout->addWidget(detailsContainer_);detailsContainer_->hide();
    connect(detailToggle_,&QPushButton::toggled,this,[this](bool expanded){detailScroll_->setVisible(expanded);detailToggle_->setIcon(lineIcon(expanded?Symbol::Chevron:Symbol::ChevronUp));detailToggle_->setToolTip(expanded?"Collapse details":"Expand details");if(const auto* e=selected())preview(e);});
    connect(detailToggle_,&QPushButton::clicked,this,[this]{if(const auto* row=selectedTorrent())showTorrentDetails(*torrents_,row->id,this);});
    thumbnails_=previewNetwork?previewNetwork:new QNetworkAccessManager(this);
    connect(sourceLink_,&QPushButton::clicked,this,[this]{const auto url=sourceLink_->property("originalPage").toUrl();if(url.isValid()&&(url.scheme()=="http"||url.scheme()=="https")&&url.userInfo().isEmpty())QDesktopServices::openUrl(url);});
    tray_=new QSystemTrayIcon(windowIcon(),this); tray_->setToolTip("Downshift"); if(QSystemTrayIcon::isSystemTrayAvailable()) tray_->show();
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::Trigger){showNormal();raise();activateWindow();}});
    theme(false); details();
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
        if(old!=current_.entries.end() && old->status!=QueueStatus::Completed) {  if(tray_->isVisible()) tray_->showMessage("Download complete",QFileInfo(e.destination).fileName(),QSystemTrayIcon::Information,5000); }
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
        for(int col=0;col<11;++col) {
            auto* item=table_->item(row,col); if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);} if(item->text()!=(col<9?text[col]:col==9?QDateTime::fromSecsSinceEpoch(e.created).toString("dd MMM"):QString())) item->setText(col<9?text[col]:col==9?QDateTime::fromSecsSinceEpoch(e.created).toString("dd MMM"):QString());
            if(col==0){item->setData(Qt::UserRole+1,(e.media?QString("Video · "):QString("HTTP · "))+QUrl(e.url).host());item->setData(Qt::UserRole+10,false);const auto category=downloadCategory(QFileInfo(e.destination).fileName());const auto suffix=QFileInfo(e.destination).suffix().toLower();item->setIcon(lineIcon(e.media||category=="Videos" ? Symbol::Video:category=="Audios" ? Symbol::Audio:category=="Zip" ? Symbol::Archive:QStringList{"png","jpg","jpeg","webp","bmp"}.contains(suffix)?Symbol::Image:QStringList{"exe","msi","msix"}.contains(suffix)?Symbol::Program:Symbol::File,QColor(category=="Videos" ? "#5286cb":category=="Audios" ? "#9871d7":category=="Zip" ? "#d69a3b":"#e91e79")));}
            if(col==3){item->setData(Qt::UserRole+3,static_cast<int>(e.status));item->setData(Qt::UserRole+1,percent);item->setData(Qt::UserRole+2,!p.totalBytes && e.status!=QueueStatus::Completed);item->setToolTip(bytes(p.downloadedBytes)+" downloaded");}
            if(col==7)item->setData(Qt::UserRole+1,static_cast<int>(e.status));if(col==9)item->setToolTip(QDateTime::fromSecsSinceEpoch(e.created).toString(Qt::ISODate));
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
    const auto countPage=[this](int page){int count=0;for(const auto& e:current_.entries)if(page==0||(page==1&&(e.status==QueueStatus::Running||e.status==QueueStatus::Paused||e.status==QueueStatus::Queued))||(page==2&&e.status==QueueStatus::Completed)||(page==3&&(e.status==QueueStatus::Failed||e.status==QueueStatus::Cancelled)))++count;for(const auto& e:torrentRows_)if(page==0||page==4||(page==1&&(torrentActive(e)||e.state==torrent::State::Paused))||(page==2&&(e.state==torrent::State::Complete||e.state==torrent::State::Seeding))||(page==3&&(e.state==torrent::State::Failed||e.state==torrent::State::Cancelled)))++count;return count;};
    int badgePage=0;for(auto* button:findChildren<QPushButton*>("navigation")){if(auto* badge=button->findChild<QLabel*>("navigationCount")){badge->setText(QString::number(countPage(badgePage)));badge->move(button->width()-28,(button->height()-22)/2);badge->setVisible(width()>=950);}++badgePage;}
    int navigationIndex=0;const Symbol symbols[]={Symbol::Download,Symbol::Activity,Symbol::Check,Symbol::Attention,Symbol::Torrent};for(auto* button:findChildren<QPushButton*>("navigation")){button->setChecked(navigationIndex==page_);button->setIcon(lineIcon(symbols[navigationIndex],QColor(navigationIndex==page_ ? "#e91e79":current_.settings.dark ? "#a8b1c2":"#697386")));++navigationIndex;}
    if(page_==4){int active=0,seeding=0;quint64 done=0,rate=0;for(const auto& row:torrentRows_){if(torrentActive(row))++active;if(row.state==torrent::State::Seeding)++seeding;done+=static_cast<quint64>(row.verifiedBytes);rate+=static_cast<quint64>(row.downloadBytesPerSecond);}summary_->setText(QString("%1 active · %2 seeding · %3 torrents").arg(active).arg(seeding).arg(torrentRows_.size()));totalMetric_->setText(QString::number(torrentRows_.size()));activeMetric_->setText(QString::number(active));speed_->setText(bytes(rate)+"/s");dataMetric_->setText(bytes(done));}
    int visible=0;
    for(int row=0;row<table_->rowCount();++row) {
        const auto display=displayOrder_[static_cast<std::size_t>(row)];const auto text=search_->text();bool status=false,match=false;
        if(display.torrent){const auto& e=torrentRows_[display.index];status=page_==0||page_==4||(page_==1&&(torrentActive(e)||e.state==torrent::State::Paused))||(page_==2&&(e.state==torrent::State::Complete||e.state==torrent::State::Seeding))||(page_==3&&(e.state==torrent::State::Failed||e.state==torrent::State::Cancelled));match=e.name.contains(text,Qt::CaseInsensitive)||e.directory.contains(text,Qt::CaseInsensitive)||QString("Torrent").contains(text,Qt::CaseInsensitive);}
        else{const auto& e=current_.entries[display.index];status=page_==0||(page_==1&&(e.status==QueueStatus::Running||e.status==QueueStatus::Queued||e.status==QueueStatus::Paused))||(page_==2&&e.status==QueueStatus::Completed)||(page_==3&&(e.status==QueueStatus::Failed||e.status==QueueStatus::Cancelled));match=e.destination.contains(text,Qt::CaseInsensitive)||e.url.contains(text,Qt::CaseInsensitive)||(e.media?QString("Video"):QString("HTTP")).contains(text,Qt::CaseInsensitive);}
        table_->setRowHidden(row,!status||!match);
        if(!table_->isRowHidden(row))++visible;
    }if(ready_){table_->setVisible(visible>0);emptyPanel_->setVisible(visible==0);empty_->setText((page_==4?torrentRows_.empty():table_->rowCount()==0) ? (page_==4?"Add your first torrent":"Ready when you are"):"No matching downloads");emptyHelp_->setText((page_==4?torrentRows_.empty():table_->rowCount()==0) ? (page_==4 ? "Add a torrent file or magnet link.":"Add a link to start your first download."):"Try another search or choose a different section.");emptyAdd_->show();}add_->setText(page_==4?"Add torrent":"Add download");emptyAdd_->setText(page_==4?"Add torrent":"Add download");video_->hide();if(auto* settings=findChild<QPushButton*>("torrentSettingsButton"))settings->setVisible(page_==4);if(auto* heading=findChild<QLabel*>("heading"))heading->setText(page_==4?"Torrents":"Downloads");details();
}
void MainWindow::details() {
    detailToggle_->setIcon(lineIcon(detailToggle_->isChecked()?Symbol::Chevron:Symbol::ChevronUp));
    if(const auto* row=selectedTorrent()){
        start_->setText(row->metadataOnly?(row->metadata?"Choose files":"Find metadata"):"Resume");start_->setEnabled(!torrentClosing_&&!torrentBusy_&&row->state!=torrent::State::Cancelled&&(row->metadataOnly||(row->state!=torrent::State::Seeding&&row->state!=torrent::State::Downloading)));
        pause_->setEnabled(!torrentClosing_&&!torrentBusy_&&torrentActive(*row));cancel_->setEnabled(!torrentClosing_&&!torrentBusy_&&row->state!=torrent::State::Cancelled&&row->state!=torrent::State::Complete&&row->state!=torrent::State::Seeding);more_->setEnabled(!torrentClosing_&&!torrentBusy_);detailsContainer_->show();detailScroll_->setVisible(detailToggle_->isChecked());detailToggle_->setCheckable(true);detailToggle_->setText("");detailToggle_->setToolTip("Open torrent overview, files, trackers and peers");
        preview(nullptr);detailTitle_->setText(detailTitle_->fontMetrics().elidedText(row->name,Qt::ElideRight,qMax(100,detailTitle_->width())));detailTitle_->setToolTip(row->name);sourceLink_->setText("Torrent · "+QString::number(row->files.size())+" files · "+torrentStateText(*row));sourceLink_->setEnabled(false);detailCopy_->setEnabled(false);detailAction_->setEnabled(start_->isEnabled()||pause_->isEnabled());detailAction_->setIcon(lineIcon(pause_->isEnabled()?Symbol::Pause:Symbol::Resume));
        const QStringList values={"Torrent",row->directory,bytes(static_cast<std::uint64_t>(row->selectedBytes)),bytes(static_cast<std::uint64_t>(row->verifiedBytes)),bytes(static_cast<std::uint64_t>(row->downloadBytesPerSecond))+"/s","↑ "+bytes(static_cast<std::uint64_t>(row->uploadBytesPerSecond))+"/s",QString("Ratio %1").arg(row->ratio,0,'f',2),row->downloadBytesPerSecond>0?duration(static_cast<double>(row->selectedBytes-row->verifiedBytes)/row->downloadBytesPerSecond):QString("—"),QString("%1 peers / %2 seeds").arg(row->peers).arg(row->seeds),bytes(static_cast<std::uint64_t>(row->verifiedBytes)),torrentStateText(*row),row->error.isEmpty()?QString("None"):row->error};
        for(std::size_t i=0;i<detailLabels_.size();++i){detailLabels_[i].second->setText(values[static_cast<int>(i)]);detailLabels_[i].second->setToolTip(values[static_cast<int>(i)]);}
        detailTabs_->setTabVisible(1,true);detailTabs_->setTabText(1,"Peers & seeding");connectionInfo_->setText(QString("%1 connected peers · %2 seeds\nUpload %3/s · Ratio %4 · Seeding %5 s\nOpen torrent details for individual peers, files and tracker status.").arg(row->peers).arg(row->seeds).arg(bytes(static_cast<std::uint64_t>(row->uploadBytesPerSecond))).arg(row->ratio,0,'f',3).arg(row->seedingSeconds));taskLog_->setText(torrentStateText(*row)+(row->error.isEmpty()?QString():"\n"+row->error));return;
    }
    detailToggle_->setCheckable(true);detailToggle_->setText("");detailToggle_->setToolTip("Expand or collapse download details");
    const auto* e=selected();start_->setText(e && (e->media || (!e->browserTransfer && !youtubeVideo(e->url).isEmpty())) ? "Retry video":"Resume"); start_->setEnabled(e && e->status!=QueueStatus::Completed && e->status!=QueueStatus::Running && e->status!=QueueStatus::Prepared); pause_->setEnabled(e && !e->media && (e->status==QueueStatus::Running || e->status==QueueStatus::Queued)); cancel_->setEnabled(e && (e->media ? e->status==QueueStatus::Running:e->status!=QueueStatus::Completed && e->status!=QueueStatus::Cancelled && e->status!=QueueStatus::Prepared)); more_->setEnabled(e!=nullptr);
    detailsContainer_->setVisible(e!=nullptr);preview(e);if(!e) {detailTitle_->setText("Select a download");for(auto& [name,label]:detailLabels_)label->setText("—");return;}
    detailScroll_->setVisible(detailToggle_->isChecked());
    const auto& p=e->progress; const auto title=e->media&&!e->mediaTitle.isEmpty()?e->mediaTitle:QFileInfo(e->destination).fileName();detailTitle_->setText(detailTitle_->fontMetrics().elidedText(title,Qt::ElideRight,qMax(100,detailTitle_->width())));detailTitle_->setToolTip(title);
    detailCopy_->setEnabled(true);detailAction_->setEnabled(e->status==QueueStatus::Completed||start_->isEnabled()||pause_->isEnabled());detailAction_->setIcon(lineIcon(e->status==QueueStatus::Completed?Symbol::Program:pause_->isEnabled()?Symbol::Pause:Symbol::Resume));detailAction_->setToolTip(e->status==QueueStatus::Completed?"Open file…":pause_->isEnabled()?"Pause":"Resume or retry");
    detailTabs_->setTabVisible(1,!e->media);detailTabs_->setTabText(1,"Connections");connectionInfo_->setText(QString("%1 active / %2 target connections\n%3 verified\n%4").arg(p.activeRanges).arg(p.targetConnections).arg(bytes(p.verifiedBytes),p.resumable?"Range checkpoints are verified before reuse.":"This source requires a fresh transfer when it cannot resume."));taskLog_->setText(statusText(e->status)+" · Added "+QDateTime::fromSecsSinceEpoch(e->created).toString(Qt::ISODate)+(e->error.isEmpty()?QString():"\n"+e->error));
    const QStringList text={QUrl(e->url).adjusted(QUrl::RemoveQuery|QUrl::RemoveFragment|QUrl::RemoveUserInfo).toDisplayString(),e->destination,p.totalBytes ? bytes(*p.totalBytes):"Unknown",bytes(p.downloadedBytes),e->status==QueueStatus::Running ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",bytes(static_cast<std::uint64_t>(p.averageBytesPerSecond))+"/s",duration(p.elapsedSeconds),p.etaSeconds ? duration(*p.etaSeconds):"—",QString("%1 active / %2 target").arg(p.activeRanges).arg(p.targetConnections),bytes(p.verifiedBytes),p.resumable ? "Supported; verified before reuse":"Requires server validation; some files need Restart",e->error.isEmpty() ? "None":e->error};
    for(std::size_t i=0;i<detailLabels_.size();++i){const auto value=e->media && i==10 ? QString("Retry starts a fresh video transfer; incomplete streams are removed."):text[static_cast<int>(i)];detailLabels_[i].second->setText(value);detailLabels_[i].second->setToolTip(value);}
}
void MainWindow::message(const QString& text,bool error) {static_cast<Notification*>(banner_)->present(text,error?Notice::Critical:Notice::Success);}
void MainWindow::watchTorrent(std::future<void> future){
    torrentBusy_=true;details();auto pending=std::make_shared<std::future<void>>(std::move(future));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();torrentBusy_=false;try{pending->get();banner_->hide();}catch(const std::exception& error){message(QString::fromUtf8(error.what()),true);}details();refresh();});timer->start();
}
void MainWindow::perform(QueueAction action) {
    if(const auto* row=selectedTorrent()){
        const auto id=row->id;if(action==QueueAction::Start){if(row->metadataOnly&&row->metadata){showTorrentDetails(*torrents_,id,this);return;}watchTorrent(torrents_->resume(id));}
        else if(action==QueueAction::Pause)watchTorrent(torrents_->pause(id));else if(action==QueueAction::Cancel)watchTorrent(torrents_->cancel(id));return;
    }
    if(const auto* e=selected()) {
    if(!e->media && !e->browserTransfer && !youtubeVideo(e->url).isEmpty() && action==QueueAction::Start){openVideo(e->url,"YouTube video");return;}
    if(e->media){if(action==QueueAction::Start)openVideo(e->url,e->mediaTitle,e->mediaFolder);else if(action==QueueAction::Cancel)for(auto* dialog:findChildren<QDialog*>("mediaDialog"))if(dialog->property("mediaUrl").toString()==e->url)if(auto* media=dynamic_cast<MediaDialog*>(dialog))media->stop();return;}
    watch(queue_.action(e->id,action),[this]{banner_->hide();});
}}
void MainWindow::dragEnterEvent(QDragEnterEvent* event){
    const auto* data=event->mimeData();const auto value=data->hasUrls()&&data->urls().size()==1?data->urls().first().toString():data->text().trimmed();const QUrl url(value);
    if(value.size()<=32768&&(url.scheme()=="http"||url.scheme()=="https"||(torrents_&&(url.scheme()=="magnet"||(url.isLocalFile()&&url.toLocalFile().endsWith(".torrent",Qt::CaseInsensitive))))))event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent* event){
    const auto* data=event->mimeData();const auto value=data->hasUrls()&&data->urls().size()==1?data->urls().first().toString():data->text().trimmed();const QUrl url(value);if(value.size()>32768)return;
    if(torrents_&&(url.scheme()=="magnet"||(url.isLocalFile()&&url.toLocalFile().endsWith(".torrent",Qt::CaseInsensitive)))){openTorrent();if(auto* source=findChild<QLineEdit*>("torrentSource"))source->setText(url.isLocalFile()?url.toLocalFile():value);event->acceptProposedAction();}
    else if(url.scheme()=="http"||url.scheme()=="https"){openAdd(value);event->acceptProposedAction();}
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);const auto width=event->size().width();if(table_){for(int column:{2,5,8})table_->setColumnHidden(column,width<1700);table_->setColumnHidden(6,width<1180);table_->setColumnHidden(9,width<1000);}if(!sidebar_)return;const bool compact=width<950;sidebar_->setMaximumWidth(compact ? 72:224);sidebar_->setProperty("compact",compact);identity_->setVisible(!compact);for(auto* button:findChildren<QPushButton*>("navigation"))button->setText(compact ? QString():button->property("navigationTitle").toString());if(auto* settings=findChild<QPushButton*>("settingsButton")){settings->setText(compact ? QString():"Settings");settings->setAccessibleName("Settings");settings->setToolTip("Settings");}if(auto* settings=findChild<QPushButton*>("torrentSettingsButton")){settings->setText(compact ? QString():"Torrent limits");settings->setAccessibleName("Torrent limits");settings->setToolTip("Torrent limits");}for(auto* button:{start_,pause_,cancel_})button->setVisible(!compact);video_->hide();for(auto* icon:findChildren<QLabel*>("metricIcon"))icon->setVisible(width>=1000);for(auto* button:findChildren<QPushButton*>("navigation"))if(auto* badge=button->findChild<QLabel*>("navigationCount")){badge->move(button->width()-28,(button->height()-22)/2);badge->setVisible(!compact);}sidebar_->style()->unpolish(sidebar_);sidebar_->style()->polish(sidebar_);
}
void MainWindow::theme(bool dark) {
    applyDesign(dark);const QColor color(dark ? "#a8b1c2":"#697386");
    int index=0;const Symbol symbols[]={Symbol::Download,Symbol::Activity,Symbol::Check,Symbol::Attention,Symbol::Torrent};for(auto* button:findChildren<QPushButton*>("navigation")){button->setIcon(lineIcon(symbols[index++],button->isChecked() ? QColor("#e91e79"):color));}
    add_->setIcon(lineIcon(Symbol::Plus,Qt::white));emptyAdd_->setIcon(lineIcon(Symbol::Plus,Qt::white));video_->setIcon(lineIcon(Symbol::Video,color));start_->setIcon(lineIcon(Symbol::Resume,color));pause_->setIcon(lineIcon(Symbol::Pause,color));cancel_->setIcon(lineIcon(Symbol::Cancel,QColor("#dc4554")));more_->setIcon(lineIcon(Symbol::More,color));detailToggle_->setIcon(lineIcon(detailToggle_->isChecked()?Symbol::Chevron:Symbol::ChevronUp,color));
    if(auto* button=findChild<QPushButton*>("settingsButton"))button->setIcon(lineIcon(Symbol::Settings,color));
    if(!search_->actions().isEmpty())search_->actions().first()->setIcon(lineIcon(Symbol::Search,color));
}
}
