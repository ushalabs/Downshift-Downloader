#include "MainWindow.h"
#include "MediaDialog.h"
#include "Brand.h"
#include "Controls.h"
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
    setObjectName("downshiftMainWindow"); setWindowTitle("Downshift"); setWindowIcon(brandIcon()); resize(860,570); setMinimumSize(700,460);
    auto* central=new QWidget(this); setCentralWidget(central); auto* shell=new QHBoxLayout(central); shell->setContentsMargins(0,0,0,0); shell->setSpacing(0);
    auto* sidebar=new QWidget; sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(150); auto* side=new QVBoxLayout(sidebar); side->setContentsMargins(12,22,12,16); side->setSpacing(8);
    auto* identity=new QLabel("Downshift"); identity->setObjectName("brand");side->addWidget(identity);side->addSpacing(24);
    const QStringList navigation={"All downloads","In progress","Completed","Needs attention"};
    for(int i=0;i<navigation.size();++i) { auto* b=makeButton(navigation[i],"navigation");b->setCheckable(true);b->setAutoExclusive(true);b->setChecked(i==0); side->addWidget(b); connect(b,&QPushButton::clicked,this,[this,i]{page_=i;filter();table_->scrollToTop();}); }
    side->addStretch(); auto* settings=makeButton("Settings","settingsButton"); side->addWidget(settings); connect(settings,&QPushButton::clicked,this,&MainWindow::openSettings);
    auto* note=new QLabel("Native. Fast. Yours."); note->setObjectName("muted"); side->addWidget(note); shell->addWidget(sidebar);
    auto* content=new QWidget; auto* layout=new QVBoxLayout(content); layout->setContentsMargins(20,18,20,14); layout->setSpacing(12); shell->addWidget(content,1);
    auto* heading=new QHBoxLayout; auto* titles=new QVBoxLayout; auto* title=new QLabel("Downloads"); title->setObjectName("heading"); titles->addWidget(title); auto* subtitle=new QLabel("Every file. One place."); subtitle->setObjectName("muted"); titles->addWidget(subtitle); heading->addLayout(titles); heading->addStretch(); auto* add=makeButton("＋  Add download","addButton"); add->setProperty("primary",true); heading->addWidget(add); connect(add,&QPushButton::clicked,this,[this]{openAdd();}); layout->addLayout(heading);
    auto* video=makeButton("Download video","videoButton");heading->insertWidget(heading->count()-1,video);connect(video,&QPushButton::clicked,this,[this]{bool accepted=false;const auto url=QInputDialog::getText(this,"Download video","Video page or public video link",QLineEdit::Normal,{},&accepted).trimmed();if(accepted && !url.isEmpty())openVideo(url,"Video");});
    auto* stats=new QHBoxLayout; summary_=new QLabel("Loading your downloads…"); summary_->setObjectName("summary"); speed_=new QLabel("0 B/s"); speed_->setObjectName("aggregateSpeed"); speed_->setMinimumWidth(150); speed_->setAlignment(Qt::AlignRight|Qt::AlignVCenter); stats->addWidget(summary_); stats->addStretch(); stats->addWidget(new QLabel("Total speed")); stats->addWidget(speed_); layout->addLayout(stats);
    auto* toolbar=new QHBoxLayout; search_=new QLineEdit; search_->setObjectName("searchField"); search_->setPlaceholderText("Search files or sources"); search_->setClearButtonEnabled(true); toolbar->addWidget(search_,1);
    view_=new QComboBox; view_->setObjectName("viewDownloads"); view_->setAccessibleName("View downloads");view_->addItems({"View downloads: Latest","View downloads: Oldest","View downloads: Size"});toolbar->addWidget(view_); start_=makeButton("Resume","startButton"); pause_=makeButton("Pause","pauseButton"); cancel_=makeButton("Cancel","cancelButton"); more_=makeButton("More ⋯","moreButton"); for(auto* b:{start_,pause_,cancel_,more_}) toolbar->addWidget(b); layout->addLayout(toolbar);
    connect(search_,&QLineEdit::textChanged,this,[this]{filter();}); connect(view_,&QComboBox::currentIndexChanged,this,[this]{apply(current_);table_->scrollToTop();});
    modernCombo(view_);
    connect(start_,&QPushButton::clicked,this,[this]{perform(QueueAction::Start);}); connect(pause_,&QPushButton::clicked,this,[this]{perform(QueueAction::Pause);}); connect(cancel_,&QPushButton::clicked,this,[this]{perform(QueueAction::Cancel);}); connect(more_,&QPushButton::clicked,this,&MainWindow::more);
    banner_=new QLabel;banner_->setTextFormat(Qt::PlainText); banner_->setObjectName("banner"); banner_->setWordWrap(true); banner_->hide(); layout->addWidget(banner_);
    auto* split=new QSplitter(Qt::Vertical); split->setChildrenCollapsible(false); layout->addWidget(split,1);
    auto* list=new QWidget; auto* listLayout=new QVBoxLayout(list); listLayout->setContentsMargins(0,0,0,0);
    empty_=new QLabel("↓\n\nReady when you are\n\nAdd a link to start your first download."); empty_->setObjectName("emptyState"); empty_->setAlignment(Qt::AlignCenter); listLayout->addWidget(empty_,1);
    table_=new QTableWidget(0,9); table_->setObjectName("downloadTable"); table_->setHorizontalHeaderLabels({"File","Size","Downloaded","Progress","Speed","Time left","Connections","Status","Priority"}); table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setShowGrid(false); table_->verticalHeader()->hide(); table_->verticalHeader()->setDefaultSectionSize(54); table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); table_->setContextMenuPolicy(Qt::CustomContextMenu); listLayout->addWidget(table_); table_->hide(); split->addWidget(list);
    connect(table_,&QTableWidget::itemSelectionChanged,this,&MainWindow::details); connect(table_,&QTableWidget::customContextMenuRequested,this,[this](const QPoint&){more();});
    auto* detail=new QWidget; detail->setObjectName("detailsPanel"); auto* dl=new QVBoxLayout(detail); dl->setContentsMargins(18,16,18,16); detailTitle_=new QLabel("Select a download"); detailTitle_->setTextFormat(Qt::PlainText); detailTitle_->setObjectName("detailTitle"); dl->addWidget(detailTitle_);
    auto* columns=new QHBoxLayout; auto* left=new QFormLayout; auto* right=new QFormLayout; columns->addLayout(left,1); columns->addSpacing(30); columns->addLayout(right,1); dl->addLayout(columns); dl->addStretch();
    const QStringList names={"Source","Destination","Size","Downloaded","Speed","Average speed","Elapsed","Time left","Connections","Verified bytes","Resume","Error"};
    for(int i=0;i<names.size();++i) { auto* label=new QLabel("—"); label->setObjectName("detail"+QString::number(i)); label->setTextFormat(Qt::PlainText); label->setTextInteractionFlags(Qt::TextSelectableByMouse); label->setWordWrap(true); label->setMaximumHeight(42); label->setMinimumWidth(0); (i<6 ? left : right)->addRow(names[i],label); detailLabels_.emplace_back(names[i],label); }
    auto* scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(detail); split->addWidget(scroll); split->setSizes({270,135});
    tray_=new QSystemTrayIcon(windowIcon(),this); tray_->setToolTip("Downshift"); if(QSystemTrayIcon::isSystemTrayAvailable()) tray_->show();
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::Trigger){showNormal();raise();activateWindow();}});
    statusBar()->showMessage("Your downloads stay on this computer."); theme(false); details();
    auto* timer=new QTimer(this); timer->setInterval(250); connect(timer,&QTimer::timeout,this,&MainWindow::refresh); timer->start(); refresh();
}
void MainWindow::refresh() {
    if(pending_) {
        if(pending_->wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        try { apply(pending_->get()); } catch(const std::exception& e) { message(QString::fromUtf8(e.what()),true); }
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
    QString selection; if(const auto* e=selected()) selection=e->id;
    current_=snapshot; ready_=true; const QSignalBlocker blocked(table_); table_->setRowCount(static_cast<int>(snapshot.entries.size()));
    table_->clearSelection();table_->setCurrentCell(-1,-1);
    displayOrder_.resize(current_.entries.size());std::iota(displayOrder_.begin(),displayOrder_.end(),std::size_t{0});
    std::sort(displayOrder_.begin(),displayOrder_.end(),[this](auto a,auto b){
        const auto& first=current_.entries[a];const auto& second=current_.entries[b];
        if(view_->currentIndex()==2 && first.progress.totalBytes.value_or(0)!=second.progress.totalBytes.value_or(0))return first.progress.totalBytes.value_or(0)>second.progress.totalBytes.value_or(0);
        if(first.created!=second.created)return view_->currentIndex()==1 ? first.created<second.created:first.created>second.created;
        return view_->currentIndex()==1 ? a<b:a>b;
    });
    int running=0,queued=0; double speed=0;
    for(int row=0;row<table_->rowCount();++row) {
        const auto& e=current_.entries[displayOrder_[static_cast<std::size_t>(row)]]; const auto& p=e.progress; const bool active=e.status==QueueStatus::Running;
        if(active){++running;speed+=p.smoothedBytesPerSecond;} if(e.status==QueueStatus::Queued)++queued;
        const auto percent=e.status==QueueStatus::Completed ? 100.0:p.totalBytes && *p.totalBytes ? std::min(e.media && active ? 99.0:100.0,100.0*static_cast<double>(p.downloadedBytes)/static_cast<double>(*p.totalBytes)) : 0;
        const QStringList text={e.media && e.status!=QueueStatus::Completed ? e.mediaTitle:QFileInfo(e.destination).fileName(),p.totalBytes ? bytes(*p.totalBytes):"—",bytes(p.downloadedBytes),"",active ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",active && p.etaSeconds ? duration(*p.etaSeconds):"—",active ? QString::number(p.activeRanges):"—",statusText(e.status),e.media ? "Video":e.priority>0 ? "High":e.priority<0 ? "Low":"Normal"};
        for(int col=0;col<9;++col) {
            if(col==3) continue; auto* item=table_->item(row,col); if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);} if(item->text()!=text[col]) item->setText(text[col]);
            if(col==0) { item->setData(Qt::UserRole,e.id); item->setToolTip(e.destination); }
            if(col==7) item->setForeground(QColor(e.status==QueueStatus::Failed ? "#ef4444":e.status==QueueStatus::Completed ? "#16a34a":current_.settings.dark ? "#cbd5e1":"#475569"));
        }
        auto* progress=qobject_cast<QProgressBar*>(table_->cellWidget(row,3)); if(!progress){progress=new QProgressBar;progress->setMaximumHeight(18);progress->setMinimumWidth(85);table_->setCellWidget(row,3,progress);} progress->setRange(0,1000);progress->setValue(static_cast<int>(percent*10)); progress->setFormat(p.totalBytes ? QString::number(percent,'f',0)+"%":"—");
        if(e.id==selection) table_->selectRow(row);
    }
    summary_->setText(QString("%1 active  ·  %2 queued  ·  %3 total").arg(running).arg(queued).arg(current_.entries.size())); speed_->setText(bytes(static_cast<std::uint64_t>(speed))+"/s"); table_->setVisible(!current_.entries.empty()); empty_->setVisible(current_.entries.empty()); filter(); details();
    table_->verticalScrollBar()->setValue(scrollPosition);
}
const QueueEntry* MainWindow::selected() const {
    const auto row=table_->currentRow(); if(row<0 || !table_->item(row,0) || table_->isRowHidden(row)) return nullptr;
    const auto id=table_->item(row,0)->data(Qt::UserRole).toString(); const auto i=std::find_if(current_.entries.begin(),current_.entries.end(),[&](const auto& e){return e.id==id;}); return i==current_.entries.end() ? nullptr : &*i;
}
void MainWindow::filter() {
    int navigationIndex=0;for(auto* button:findChildren<QPushButton*>("navigation"))button->setChecked(navigationIndex++==page_);
    for(int row=0;row<table_->rowCount();++row) {
        const auto& e=current_.entries[displayOrder_[static_cast<std::size_t>(row)]]; const int f=page_;
        const bool status=f==0 || (f==1 && (e.status==QueueStatus::Running || e.status==QueueStatus::Queued || e.status==QueueStatus::Paused)) || (f==2 && e.status==QueueStatus::Completed) || (f==3 && (e.status==QueueStatus::Failed || e.status==QueueStatus::Cancelled));
        const auto text=search_->text(); table_->setRowHidden(row,!status || (!e.destination.contains(text,Qt::CaseInsensitive) && !e.url.contains(text,Qt::CaseInsensitive)));
    } details();
}
void MainWindow::details() {
    const auto* e=selected();start_->setText(e && (e->media || !youtubeVideo(e->url).isEmpty()) ? "Retry video":"Resume"); start_->setEnabled(e && e->status!=QueueStatus::Completed && e->status!=QueueStatus::Running && e->status!=QueueStatus::Prepared); pause_->setEnabled(e && !e->media && (e->status==QueueStatus::Running || e->status==QueueStatus::Queued)); cancel_->setEnabled(e && (e->media ? e->status==QueueStatus::Running:e->status!=QueueStatus::Completed && e->status!=QueueStatus::Cancelled && e->status!=QueueStatus::Prepared)); more_->setEnabled(e!=nullptr);
    if(!e) {detailTitle_->setText("Select a download");for(auto& [name,label]:detailLabels_)label->setText("—");return;}
    const auto& p=e->progress; detailTitle_->setText((e->media && e->status!=QueueStatus::Completed ? e->mediaTitle:QFileInfo(e->destination).fileName())+"  ·  "+statusText(e->status));
    const QStringList text={e->url,e->destination,p.totalBytes ? bytes(*p.totalBytes):"Unknown",bytes(p.downloadedBytes),e->status==QueueStatus::Running ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",bytes(static_cast<std::uint64_t>(p.averageBytesPerSecond))+"/s",duration(p.elapsedSeconds),p.etaSeconds ? duration(*p.etaSeconds):"—",QString("%1 active / %2 target").arg(p.activeRanges).arg(p.targetConnections),bytes(p.verifiedBytes),p.resumable ? "Supported; verified before reuse":"Requires server validation; some files need Restart",e->error.isEmpty() ? "None":e->error};
    for(std::size_t i=0;i<detailLabels_.size();++i){const auto value=e->media && i==10 ? QString("Use Retry video; partial streams are kept where the site permits."):text[static_cast<int>(i)];detailLabels_[i].second->setText(value);detailLabels_[i].second->setToolTip(value);}
}
void MainWindow::message(const QString& text,bool error) {banner_->setText(text);banner_->setProperty("error",error);banner_->show();banner_->style()->unpolish(banner_);banner_->style()->polish(banner_);statusBar()->showMessage(text,10000);}
void MainWindow::perform(QueueAction action) {if(const auto* e=selected()) {
    if(!e->media && !youtubeVideo(e->url).isEmpty() && action==QueueAction::Start){openVideo(e->url,"YouTube video");return;}
    if(e->media){if(action==QueueAction::Start)openVideo(e->url,e->mediaTitle,e->mediaFolder);else if(action==QueueAction::Cancel)for(auto* dialog:findChildren<QDialog*>("mediaDialog"))if(dialog->property("mediaUrl").toString()==e->url)if(auto* media=dynamic_cast<MediaDialog*>(dialog))media->stop();return;}
    watch(queue_.action(e->id,action),[this]{banner_->hide();});
}}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);if(table_)for(int column:{2,5,6,8})table_->setColumnHidden(column,event->size().width()<1100);
}
void MainWindow::theme(bool dark) {
    const QString bg=dark ? "#211522":"#fff6fa",surface=dark ? "#302033":"#ffffff",text=dark ? "#f7e8f0":"#382338",muted=dark ? "#bda1b5":"#80667b",border=dark ? "#50324a":"#f1dce7";
    setStyleSheet(QString("QWidget{font-family:'Segoe UI';font-size:12px;color:%1;background:%2;} QMainWindow,QDialog{background:%2;} QWidget#sidebar{background:%3;border-right:1px solid %5;} QLabel#brand{font-size:20px;font-weight:700;color:#ec4899;background:transparent;} QLabel#heading{font-size:25px;font-weight:700;} QLabel#detailTitle{font-size:15px;font-weight:600;background:transparent;} QLabel#muted{color:%4;background:transparent;} QLabel#aggregateSpeed{font-size:20px;font-weight:600;color:#ec4899;} QPushButton{background:%3;border:1px solid %5;border-radius:7px;padding:7px 12px;} QPushButton:hover{border-color:#ec4899;} QPushButton:disabled{color:%4;background:%2;} QPushButton[primary=true]{background:#db2777;color:white;border-color:#db2777;font-weight:600;} QPushButton[primary=true]:hover{background:#be185d;} QPushButton#navigation{text-align:left;border:0;background:transparent;padding:10px 8px;} QPushButton#navigation:hover{background:%2;} QLineEdit,QComboBox,QSpinBox{background:%3;border:1px solid %5;border-radius:7px;padding:8px;selection-background-color:#db2777;} QLineEdit:focus{border-color:#ec4899;} QTableWidget{background:%3;border:1px solid %5;border-radius:10px;selection-background-color:%6;selection-color:%1;} QTableWidget::item{padding:8px;border-bottom:1px solid %5;background:transparent;} QHeaderView::section{background:%3;color:%4;border:0;border-bottom:1px solid %5;padding:12px 8px;font-weight:600;} QProgressBar{background:%5;border:0;border-radius:5px;text-align:center;color:%1;font-size:10px;} QProgressBar::chunk{background:#f472b6;border-radius:5px;} QWidget#detailsPanel{background:%3;border:1px solid %5;border-radius:10px;} QWidget#detailsPanel QLabel{background:transparent;} QLabel#emptyState{background:%3;border:1px solid %5;border-radius:10px;color:%4;font-size:18px;} QLabel#banner{background:%6;border-radius:7px;padding:10px;} QLabel#banner[error=true]{color:#ef4444;} QStatusBar{color:%4;border-top:1px solid %5;} QMenu{background:%3;border:1px solid %5;padding:6px;} QMenu::item{padding:9px 22px;} QMenu::item:selected{background:%6;} QScrollArea{background:transparent;} QSplitter::handle{background:transparent;} QToolTip{color:%1;background:%3;border:1px solid %5;}")
        .arg(text,bg,surface,muted,border,dark ? "#50253e":"#fce7f3"));
    setStyleSheet(styleSheet()+QString(
        "QPushButton{font-family:Segoe UI;font-weight:600;font-size:13px;border-radius:11px;padding:9px 14px;min-height:22px;}"
        "QPushButton:hover{background:%1;border-color:#f472b6;} QPushButton:pressed{background:%2;}"
        "QPushButton[primary=true]{background:#db2777;border:0;padding:10px 18px;} QPushButton[primary=true]:hover{background:#ec4899;} QPushButton[primary=true]:pressed{background:#be185d;}"
        "QPushButton:disabled{border-color:transparent;color:%3;} QPushButton[primary=true]:disabled{background:%2;color:%3;}"
        "QPushButton#navigation:checked{background:%1;color:#db2777;border:0;border-radius:10px;font-weight:700;}"
        "QLineEdit,QComboBox,QSpinBox{font-family:Segoe UI;font-size:13px;border-radius:10px;padding:10px;min-height:20px;}"
        "QComboBox{padding-right:34px;} QComboBox::drop-down{width:30px;border:0;background:transparent;} QComboBox::down-arrow{image:url(:/branding/chevron.xpm);width:14px;height:14px;}"
        "QComboBox QAbstractItemView{background:%4;border:1px solid %2;border-radius:10px;padding:6px;selection-background-color:%1;selection-color:%5;outline:0;}"
        "QComboBox QAbstractItemView::item{border:0;border-radius:7px;min-height:22px;margin:2px;padding:8px 12px;} QComboBox QAbstractItemView::item:selected{background:%1;color:#db2777;}"
        "QMenu::item{border-radius:7px;margin:2px;} QPushButton:default{background:#db2777;color:white;border-color:#db2777;}"
        "QHeaderView::section{font-family:Segoe UI;font-size:12px;padding:10px 8px;} QTableWidget{border-radius:14px;}"
        "QWidget#detailsPanel{border-radius:14px;} QLabel#mediaTitle{font-family:Segoe UI;font-size:20px;font-weight:700;background:transparent;}"
        "QDialog QLabel{background:transparent;} QDialog#mediaDialog{background:%4;} QProgressBar{border-radius:7px;min-height:8px;} QProgressBar::chunk{border-radius:7px;background:#ec4899;}"
        "QScrollBar:vertical{background:transparent;width:9px;margin:2px;} QScrollBar::handle:vertical{background:%2;border-radius:4px;min-height:24px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        ).arg(dark ? "#50253e":"#fce7f3",border,muted,surface,text));
    qApp->setStyleSheet(styleSheet());
}
}
