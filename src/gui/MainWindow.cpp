#include "MainWindow.h"
#include "MediaDialog.h"
#include "TorrentDialogs.h"
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QStandardPaths>
#include "Brand.h"
#include "Controls.h"
#include "Design.h"
#include "DownloadDelegate.h"
#include "core/DownloadQueue/Categories.h"
#include "core/DownloadQueue/MediaContext.h"
#include "core/DownloadQueue/MediaResume.h"
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
#include <QStackedWidget>
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
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <tuple>
namespace downshift {
namespace {
constexpr int firstFilePage=5;
constexpr int filePage(FileCategory category){return firstFilePage+static_cast<int>(category);}
struct FileNavigation {const char* title;FileCategory category;Symbol symbol;};
constexpr FileNavigation fileNavigation[]={
    {"Audio",FileCategory::Audio,Symbol::Audio},
    {"Archives",FileCategory::Archive,Symbol::Archive},
    {"Videos",FileCategory::Video,Symbol::Video},
    {"Programs",FileCategory::Program,Symbol::Program},
    {"Documents",FileCategory::Document,Symbol::File},
    {"APKs",FileCategory::Apk,Symbol::Apk},
    {"Images",FileCategory::Image,Symbol::Image}
};
FileCategory categoryFor(const QueueEntry& entry){
    return entry.media?FileCategory::Video:fileCategory(entry.destination);
}
QString rowKey(const QString& id,bool torrent){return (torrent?"torrent:":"file:")+id;}
QString rowKey(const QTableWidgetItem* item){
    return item?rowKey(item->data(Qt::UserRole).toString(),item->data(Qt::UserRole+10).toBool()):QString();
}

class TransferCommand final : public QPushButton {
public:
    TransferCommand(const QString& text,const char* name,int width):QPushButton(text) {
        setObjectName(name);
        setProperty("transferCommand",true);
        setFixedSize(width,48);
        setIconSize(QSize(20,20));
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        QStylePainter painter(this);
        painter.drawControl(QStyle::CE_PushButtonBevel,option);
        const auto size=iconSize();
        icon().paint(&painter,QRect(QPoint((width()-size.width())/2,6),size),Qt::AlignCenter,
                     isEnabled()?QIcon::Normal:QIcon::Disabled);
        const auto tokens=DesignTokens::theme(qApp->property("downshiftDark").toBool());
        painter.setPen(isEnabled()?tokens.text:palette().color(QPalette::Disabled,QPalette::ButtonText));
        painter.drawText(rect().adjusted(4,27,-4,-2),Qt::AlignCenter,text());
    }
};

class DownloadTable final:public QTableWidget {
    bool adjusting_=false,pending_=false;
    std::function<void()> deleteSelected_;
    void resizeEvent(QResizeEvent* event) override {
        QTableWidget::resizeEvent(event);
        fitColumns();
    }
    void alignScrollBar(){
        auto* container=verticalScrollBar()->parentWidget();
        if(!container||adjusting_)return;
        auto area=container->geometry();area.setTop(viewport()->mapTo(container->parentWidget(),QPoint{}).y());
        if(area==container->geometry()||area.height()<1)return;
        adjusting_=true;container->setGeometry(area);adjusting_=false;
    }
    bool eventFilter(QObject* object,QEvent* event)override{
        if(!adjusting_&&!pending_&&(event->type()==QEvent::Resize||event->type()==QEvent::Move)){
            pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;alignScrollBar();});
        }
        return QTableWidget::eventFilter(object,event);
    }
    void updateGeometries()override{QTableWidget::updateGeometries();alignScrollBar();}
    void mousePressEvent(QMouseEvent* event)override{setProperty("detailsAllowed",event->button()!=Qt::RightButton);QTableWidget::mousePressEvent(event);}
    void keyPressEvent(QKeyEvent* event)override{
        setProperty("detailsAllowed",true);
        if(event->key()==Qt::Key_Delete&&event->modifiers()==Qt::NoModifier){
            if(!event->isAutoRepeat())deleteSelected_();
            event->accept();return;
        }
        QTableWidget::keyPressEvent(event);
    }
public:
    void fitColumns() {
        // Reserve a scrollbar even when it is hidden so live row counts don't
        // shift the table's columns. Keep the core transfer fields visible.
        int remaining=width()-2*frameWidth()-verticalScrollBar()->sizeHint().width();
        for(int column:{1,3,4,5,7,10})remaining-=columnWidth(column);
        for(const auto& extra:{std::tuple{9,68,224},std::tuple{6,88,256},std::tuple{2,88,288},std::tuple{8,100,288}}){
            const auto [column,size,minimumName]=extra;
            const bool show=remaining>=size+minimumName;
            if(isColumnHidden(column)==show)setColumnHidden(column,!show);
            if(show){setColumnWidth(column,size);remaining-=size;}
        }
    }
    explicit DownloadTable(std::function<void()> deleteSelected):QTableWidget(0,11),deleteSelected_(std::move(deleteSelected)){
        setProperty("detailsAllowed",true);verticalScrollBar()->parentWidget()->installEventFilter(this);
    }
};
QString bytes(std::uint64_t n) {
    double value=static_cast<double>(n); const char* units[]={"B","KiB","MiB","GiB","TiB"}; int unit=0;
    while(value>=1024 && unit<4) {value/=1024; ++unit;} return QString("%1 %2").arg(value,0,'f',unit ? 1 : 0).arg(units[unit]);
}

}
void MainWindow::openVideo(QString address,QString title,QString folder,QJsonObject context,QString filename) {
    if(!validMediaContext(context)){message("The video request context is invalid. Refresh its page and retry.",true);return;}
    if(context.contains("youtube") && youtubeVideo(address)!="https://www.youtube.com/watch?v="+context.value("youtube").toObject().value("id").toString()){message("The browser video no longer matches this page. Refresh its page and retry.",true);return;}
    if(youtubeHost(QUrl(address).host())) {
        const auto canonical=youtubeVideo(address);
        if(canonical.isEmpty()){message("Open a specific YouTube video and use its Download this video button, or paste its watch/shorts link.",true);return;}
        address=canonical;
    }
    const QUrl url(address);
    if(!url.isValid() || (url.scheme()!="https" && url.scheme()!="http") || url.host().isEmpty() || !url.userInfo().isEmpty() || address.size()>32768 || address.contains(QChar(0))){message("Enter an HTTP or HTTPS video address without a username or password.",true);return;}
    int count=0;for(auto* child:findChildren<QDialog*>(QString(),Qt::FindDirectChildrenOnly))if(auto* dialog=dynamic_cast<MediaDialog*>(child)){++count;if(dialog->property("mediaUrl").toString()==address){dialog->refreshBrowserSource(context);dialog->present();return;}}
    if(count>=4){message("Finish or cancel an existing video task before opening another.",true);return;}
    auto* dialog=new MediaDialog(queue_,std::move(address),std::move(title),this,std::move(folder),std::move(context));dialog->setDownloadName(filename);dialog->present();
}
void MainWindow::showDownloads(){showNormal();raise();activateWindow();for(auto* child:findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(child->property("showFromTray").toBool()){child->setProperty("showFromTray",false);child->show();}}
void MainWindow::resumeVideo(const QueueEntry& entry){
    for(auto* child:findChildren<QDialog*>("mediaDialog"))if(child->property("mediaId").toString()==entry.id)if(auto* media=dynamic_cast<MediaDialog*>(child)){media->resume();return;}
    if(!validMediaResume(entry.videoMetadata.value("resumeRecipe").toObject())){openVideo(entry.url,entry.mediaTitle,entry.mediaFolder,entry.videoMetadata.value("requestContext").toObject(),entry.videoMetadata.value("downloadName").toString());return;}
    auto* media=new MediaDialog(queue_,entry.url,entry.mediaTitle,this,entry.mediaFolder,entry.videoMetadata.value("requestContext").toObject(),entry);media->resume();media->present();
}
void MainWindow::requestExit(){
    if(exitRequested_){close();return;}
    const bool active=std::any_of(current_.entries.begin(),current_.entries.end(),[](const auto& e){return e.status==QueueStatus::Running||e.status==QueueStatus::Queued;})||std::any_of(torrentRows_.begin(),torrentRows_.end(),[](const auto& e){return torrentActive(e);});
    bool mediaActive=false;for(auto* child:findChildren<QDialog*>("mediaDialog"))if(auto* media=dynamic_cast<MediaDialog*>(child);media&&media->active())mediaActive=true;
    if((active||mediaActive)&&QMessageBox::question(this,"Exit Downshift","Exit Downshift and stop ongoing downloads? Choose No to keep downloading in the background.",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    exitRequested_=true;QApplication::setQuitOnLastWindowClosed(true);close();
}
QPushButton* makeButton(const QString& text,const char* name,QWidget* parent) { auto* b=new QPushButton(text,parent); b->setObjectName(name); b->setMinimumHeight(DesignTokens::controlHeight); return b; }
MainWindow::MainWindow(QueueManager& queue,QWidget* parent,QNetworkAccessManager* previewNetwork):QMainWindow(parent),queue_(queue) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));QApplication::setWindowIcon(brandIcon());
    setAcceptDrops(true);setObjectName("downshiftMainWindow"); setWindowTitle("Downshift"); setWindowIcon(brandIcon()); resize(1075,730); setMinimumSize(800,550);
    auto* central=new QWidget(this);setCentralWidget(central);
    auto* frame=new QVBoxLayout(central);frame->setContentsMargins(0,0,0,0);frame->setSpacing(0);
    auto* masthead=new QWidget; masthead->setObjectName("masthead");
    auto* branding=new QHBoxLayout(masthead);branding->setContentsMargins(14,9,16,9);branding->setSpacing(10);
    auto* mark=new QLabel;mark->setPixmap(brandIcon().pixmap(24,24));branding->addWidget(mark);
    auto* brand=new QLabel("Downshift");brand->setObjectName("brand");branding->addWidget(brand);branding->addStretch();
    auto* summary=new QWidget;summary->setObjectName("summaryBar");branding->addWidget(summary);
    auto* stats=new QHBoxLayout(summary);stats->setContentsMargins(0,0,0,0);stats->setSpacing(12);
    const auto metric=[&](const QString& caption,const char* name){
        if(stats->count()){
            auto* separator=new QFrame;separator->setObjectName("summarySeparator");separator->setFixedSize(1,12);
            stats->addWidget(separator,0,Qt::AlignVCenter);
        }
        auto* group=new QHBoxLayout;group->setSpacing(6);
        auto* label=new QLabel(caption);label->setObjectName("muted");group->addWidget(label);
        auto* value=new QLabel("—");value->setObjectName(name);value->setProperty("metric",true);value->setAccessibleName(caption);group->addWidget(value);
        stats->addLayout(group);return value;
    };
    totalMetric_=metric("Downloads","totalMetric");activeMetric_=metric("Active","activeMetric");
    speed_=metric("Total speed","aggregateSpeed");dataMetric_=metric("Total downloaded","dataMetric");
    dataMetric_->setToolTip("Bytes currently retained across tasks, including paused partials; not a lifetime traffic counter.");
    frame->addWidget(masthead);

    auto* body=new QWidget;frame->addWidget(body,1);
    auto* shell=new QHBoxLayout(body);shell->setContentsMargins(0,0,0,0);shell->setSpacing(0);
    sidebar_=new QWidget;sidebar_->setObjectName("sidebar");sidebar_->setFixedWidth(184);
    auto* side=new QVBoxLayout(sidebar_);side->setContentsMargins(8,12,8,8);side->setSpacing(4);
    auto* navigationPanel=new QWidget;navigationPanel->setObjectName("navigationPanel");side->addWidget(navigationPanel);
    auto* navigation=new QVBoxLayout(navigationPanel);navigation->setContentsMargins(4,6,4,6);navigation->setSpacing(1);
    auto* allDownloads=new QHBoxLayout;allDownloads->setContentsMargins(0,0,0,0);allDownloads->setSpacing(1);
    allDownloads->addWidget(navigationButton("All downloads",0,Symbol::Folder),1);
    categoryToggle_=makeButton("","categoryToggle");categoryToggle_->setProperty("tertiary",true);
    categoryToggle_->setFixedWidth(24);categoryToggle_->setIconSize(QSize(14,14));categoryToggle_->setCheckable(true);categoryToggle_->setChecked(true);
    categoryToggle_->setAccessibleName("Hide download categories");categoryToggle_->setToolTip(categoryToggle_->accessibleName());
    allDownloads->addWidget(categoryToggle_);navigation->addLayout(allDownloads);
    categoryNav_=new QWidget;categoryLayout_=new QVBoxLayout(categoryNav_);categoryLayout_->setContentsMargins(18,2,0,3);categoryLayout_->setSpacing(0);
    for(const auto& [title,category,symbol]:fileNavigation)
        categoryLayout_->addWidget(navigationButton(title,filePage(category),symbol));
    navigation->addWidget(categoryNav_);
    navigation->addSpacing(4);
    navigation->addWidget(navigationButton("In progress",1,Symbol::Activity));
    navigation->addWidget(navigationButton("Completed",2,Symbol::Check));
    navigation->addWidget(navigationButton("Needs attention",3,Symbol::Attention));
    connect(categoryToggle_,&QPushButton::toggled,this,[this](bool open){
        categoryNav_->setVisible(open);
        categoryToggle_->setAccessibleName(open?"Hide download categories":"Show download categories");
        categoryToggle_->setToolTip(categoryToggle_->accessibleName());
        categoryToggle_->setIcon(lineIcon(open?Symbol::ChevronUp:Symbol::Chevron,DesignTokens::theme(current_.settings.dark).muted));
    });
    side->addStretch();
    auto* settings=makeButton("Settings","settingsButton");settings->setProperty("railCommand",true);settings->setIconSize(QSize(16,16));side->addWidget(settings);
    connect(settings,&QPushButton::clicked,this,&MainWindow::openSettings);shell->addWidget(sidebar_);

    auto* content=new QWidget;auto* layout=new QVBoxLayout(content);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);shell->addWidget(content,1);
    auto* commands=new QWidget;commands->setObjectName("commandBar");
    auto* toolbar=new QHBoxLayout(commands);toolbar->setContentsMargins(12,6,12,6);toolbar->setSpacing(6);
    const auto divideCommands=[&]{
        auto* separator=new QFrame;
        separator->setObjectName("summarySeparator");
        separator->setFixedSize(1,32);
        toolbar->addWidget(separator,0,Qt::AlignVCenter);
    };
    video_=makeButton("Download video","videoButton",this);video_->hide();video_->setEnabled(false);
    add_=makeButton("Add URL","addButton");add_->setProperty("primary",true);add_->setEnabled(false);add_->setToolTip("Add a download (Ctrl+N)");
    add_->setFixedWidth(128);add_->setIconSize(QSize(18,18));toolbar->addWidget(add_);
    connect(add_,&QPushButton::clicked,this,[this]{if(page_==4)openTorrent();else openAdd();});
    divideCommands();
    start_=new TransferCommand("Resume","startButton",96);pause_=new TransferCommand("Pause","pauseButton",56);
    cancel_=new TransferCommand("Cancel","cancelButton",56);more_=new TransferCommand("More","moreButton",50);
    more_->setAccessibleName("More download actions");more_->setToolTip("More download actions");
    for(auto* button:{start_,pause_,cancel_,more_})toolbar->addWidget(button);
    divideCommands();
    search_=new QLineEdit;search_->setObjectName("searchField");search_->setPlaceholderText("Search downloads");search_->setClearButtonEnabled(true);search_->setMinimumWidth(128);
    search_->setAccessibleName("Search downloads");search_->setToolTip("Search filenames and sources (Ctrl+F)");search_->addAction(lineIcon(Symbol::Search),QLineEdit::LeadingPosition);toolbar->addWidget(search_,1);
    view_=new QComboBox;view_->setObjectName("viewDownloads");view_->setAccessibleName("Sort downloads");view_->setToolTip("Sort downloads by time or size");
    view_->addItems({"Latest","Oldest","Size"});view_->setFixedWidth(96);toolbar->addWidget(view_);layout->addWidget(commands);
    connect(search_,&QLineEdit::textChanged,this,[this]{filter();}); connect(view_,&QComboBox::currentIndexChanged,this,[this]{apply(current_);table_->scrollToTop();});
    modernCombo(view_);
    connect(start_,&QPushButton::clicked,this,[this]{perform(QueueAction::Start);}); connect(pause_,&QPushButton::clicked,this,[this]{perform(QueueAction::Pause);}); connect(cancel_,&QPushButton::clicked,this,[this]{perform(QueueAction::Cancel);}); connect(more_,&QPushButton::clicked,this,&MainWindow::more);
    banner_=new Notification(content);
    auto* list=new QWidget; auto* listLayout=new QVBoxLayout(list); listLayout->setContentsMargins(0,0,0,0);
    emptyPanel_=new QWidget;emptyPanel_->setObjectName("emptyPanel");auto* emptyLayout=new QVBoxLayout(emptyPanel_);emptyLayout->setAlignment(Qt::AlignCenter);emptyLayout->setSpacing(8);auto* illustration=new QLabel;illustration->setAlignment(Qt::AlignCenter);illustration->setPixmap(lineIcon(Symbol::Download).pixmap(24,24));emptyLayout->addWidget(illustration);empty_=new QLabel("Loading saved downloads…");empty_->setObjectName("emptyState");empty_->setAlignment(Qt::AlignCenter);emptyLayout->addWidget(empty_);emptyHelp_=new QLabel("Your history is being opened.");emptyHelp_->setWordWrap(true);emptyHelp_->setAlignment(Qt::AlignCenter);emptyHelp_->setObjectName("muted");emptyLayout->addWidget(emptyHelp_);emptyAdd_=makeButton("Add download","emptyAdd");emptyAdd_->setProperty("primary",true);emptyAdd_->setEnabled(false);emptyLayout->addWidget(emptyAdd_,0,Qt::AlignCenter);connect(emptyAdd_,&QPushButton::clicked,this,[this]{if(page_==4)openTorrent();else openAdd();});listLayout->addWidget(emptyPanel_,1);
    table_=new DownloadTable([this]{deleteSelection();});table_->setObjectName("downloadTable");
    table_->setHorizontalHeaderLabels({"Name / Source","Size","Downloaded","Progress","Speed","ETA","Connections","Status","Type / Priority","Added",""});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setShowGrid(false);table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(DesignTokens::rowHeight);
    auto* header=table_->horizontalHeader();header->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);header->setMinimumSectionSize(28);
    header->setSectionResizeMode(QHeaderView::Fixed);header->setSectionResizeMode(0,QHeaderView::Stretch);
    for(const auto& [column,size]:{std::pair{1,76},std::pair{3,104},std::pair{4,84},std::pair{5,64},std::pair{10,32}})table_->setColumnWidth(column,size);
    for(int column:{1,2,4,5,6})table_->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
    table_->setItemDelegate(new DownloadDelegate(table_));table_->setColumnWidth(7,DownloadDelegate::statusColumnWidth(table_->font()));
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);table_->setContextMenuPolicy(Qt::CustomContextMenu);table_->setMouseTracking(true);
    listLayout->addWidget(table_);table_->hide();layout->addWidget(list,1);
    for(QWidget* surface:{central,masthead,summary,body,sidebar_,navigationPanel,categoryNav_,content,
                          commands,list,emptyPanel_,table_->viewport(),header->viewport()})
        surface->installEventFilter(this);
    connect(table_,&QTableWidget::cellClicked,this,[this](int,int col){if(!table_->property("detailsAllowed").toBool())return;if(col==10)more();else details();});
    connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int,int){if(const auto* e=selected();e&&e->status==QueueStatus::Completed){const auto file=e->destination;auto* confirm=new QMessageBox(QMessageBox::Question,"Open downloaded file","Open this file with its Windows application?\n\n"+QFileInfo(file).fileName(),QMessageBox::Yes|QMessageBox::No,this);confirm->setAttribute(Qt::WA_DeleteOnClose);confirm->setDefaultButton(QMessageBox::No);connect(confirm,&QMessageBox::finished,this,[this,file](int answer){if(answer==QMessageBox::Yes&&!QDesktopServices::openUrl(QUrl::fromLocalFile(file)))message("Windows could not open this file.",true);});confirm->open();}});
    connect(table_,&QTableWidget::itemSelectionChanged,this,&MainWindow::details); connect(table_,&QTableWidget::customContextMenuRequested,this,[this](const QPoint&){more();});
    retryHistory_=makeButton("Retry loading","retryHistory");retryHistory_->hide();emptyLayout->addWidget(retryHistory_,0,Qt::AlignCenter);connect(retryHistory_,&QPushButton::clicked,this,[this]{retryHistory_->setEnabled(false);empty_->setText("Loading saved downloads…");emptyHelp_->setText("Opening the existing database; download files are preserved.");watch(queue_.retryLoading(),[this]{banner_->hide();refresh();});});
    emptyHelp_->setMaximumWidth(480);emptyHelp_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    detailsContainer_=new QWidget;detailsContainer_->setObjectName("detailsPanel");auto* detailLayout=new QVBoxLayout(detailsContainer_);detailLayout->setContentsMargins(8,8,8,8);detailLayout->setSpacing(4);
    auto* detailHeader=new QHBoxLayout;detailHeader->setSpacing(8);thumbnail_=new QLabel;thumbnail_->setObjectName("previewFallback");thumbnail_->setFixedSize(128,72);thumbnail_->setAlignment(Qt::AlignCenter);detailHeader->addWidget(thumbnail_);thumbnail_->hide();
    auto* titleBox=new QVBoxLayout;titleBox->setSpacing(2);detailTitle_=new QLabel("Select a download");detailTitle_->setObjectName("detailTitle");detailTitle_->setTextFormat(Qt::PlainText);detailTitle_->setMinimumWidth(0);titleBox->addWidget(detailTitle_);
    sourceLink_=makeButton("","sourceLink");sourceLink_->setMinimumHeight(18);sourceLink_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);sourceLink_->setIcon(lineIcon(Symbol::Link,QColor("#EC1680")));sourceLink_->setIconSize(QSize(16,16));titleBox->addWidget(sourceLink_);videoInfo_=new QLabel;videoInfo_->setObjectName("videoMetadata");videoInfo_->setTextFormat(Qt::PlainText);videoInfo_->setObjectName("muted");titleBox->addWidget(videoInfo_);detailHeader->addLayout(titleBox,1);
    detailAction_=makeButton("","detailAction");detailAction_->setFixedWidth(36);detailAction_->setToolTip("Pause or resume selected download");detailHeader->addWidget(detailAction_);connect(detailAction_,&QPushButton::clicked,this,[this]{if(const auto* e=selected();e&&e->status==QueueStatus::Completed){emit table_->cellDoubleClicked(table_->currentRow(),0);return;}perform(pause_->isEnabled()?QueueAction::Pause:QueueAction::Start);});
    detailFolder_=makeButton("","detailFolder");detailFolder_->setFixedWidth(36);detailFolder_->setIcon(lineIcon(Symbol::Folder));detailFolder_->setToolTip("Open destination folder");detailHeader->addWidget(detailFolder_);connect(detailFolder_,&QPushButton::clicked,this,[this]{QString folder;if(const auto* e=selected())folder=e->media&&e->status!=QueueStatus::Completed?e->mediaFolder:QFileInfo(e->destination).absolutePath();else if(const auto* row=selectedTorrent())folder=row->directory;if(!folder.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(folder));});
    detailCopy_=makeButton("","detailCopy");detailCopy_->setFixedWidth(36);detailCopy_->setIcon(lineIcon(Symbol::Copy));detailCopy_->setToolTip("Copy source link");detailHeader->addWidget(detailCopy_);connect(detailCopy_,&QPushButton::clicked,this,[this]{if(const auto* e=selected())QApplication::clipboard()->setText(e->videoMetadata.value("originalPage").toString(e->url));});
    detailToggle_=makeButton("","detailsToggle");detailToggle_->setAccessibleName("Expand or collapse download details");detailToggle_->setToolTip("Expand or collapse download details");detailToggle_->setFixedWidth(36);detailToggle_->setCheckable(true);detailToggle_->setChecked(true);detailHeader->addWidget(detailToggle_);auto* closeDetails=makeButton("","closeDetails");closeDetails->setFixedWidth(36);closeDetails->setIcon(lineIcon(Symbol::Cancel));closeDetails->setAccessibleName("Close download details");closeDetails->setToolTip("Close details");detailHeader->addWidget(closeDetails);for(auto* button:{detailAction_,detailFolder_,detailCopy_,detailToggle_,closeDetails})button->setIconSize(QSize(16,16));connect(closeDetails,&QPushButton::clicked,this,&MainWindow::clearDownloadSelection);detailLayout->addLayout(detailHeader);
    auto* detail=new QWidget;auto* dl=new QVBoxLayout(detail);dl->setContentsMargins(0,0,0,0);detailTabs_=new QTabWidget;detailTabs_->setObjectName("downloadDetailsTabs");dl->addWidget(detailTabs_);auto* overview=new QWidget;auto* grid=new QGridLayout(overview);grid->setContentsMargins(2,6,2,2);grid->setHorizontalSpacing(16);grid->setVerticalSpacing(3);
    const QStringList names={"Source","Destination","Size","Downloaded","Speed","Average speed","Elapsed","Time left","Connections","Verified bytes","Resume","Error"};
    for(int i=0;i<names.size();++i){auto* label=new QLabel("—");label->setObjectName("detail"+QString::number(i));label->setTextFormat(Qt::PlainText);label->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);label->setMinimumWidth(0);label->setWordWrap(true);label->setMaximumHeight(36);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);auto* caption=new QLabel(names[i]);caption->setObjectName("muted");const int column=i%4,row=(i/4)*2;grid->addWidget(caption,row,column);grid->addWidget(label,row+1,column);grid->setColumnStretch(column,1);detailLabels_.emplace_back(names[i],label);}
    detailTabs_->addTab(overview,"Overview");connectionInfo_=new QLabel;connectionInfo_->setWordWrap(true);connectionInfo_->setMargin(10);detailTabs_->addTab(connectionInfo_,"Connections");taskLog_=new QLabel;taskLog_->setTextFormat(Qt::PlainText);taskLog_->setWordWrap(true);taskLog_->setMargin(10);detailTabs_->addTab(taskLog_,"Task status");torrentDetailsStack_=new QStackedWidget;dl->addWidget(torrentDetailsStack_);torrentDetailsStack_->hide();
    detailScroll_=new QScrollArea;detailScroll_->setWidgetResizable(true);detailScroll_->setFrameShape(QFrame::NoFrame);detailScroll_->setWidget(detail);detailScroll_->setMaximumHeight(168);detailLayout->addWidget(detailScroll_);layout->addWidget(detailsContainer_);detailsContainer_->hide();
    connect(detailToggle_,&QPushButton::toggled,this,[this](bool expanded){detailScroll_->setVisible(expanded);detailToggle_->setIcon(lineIcon(expanded?Symbol::Chevron:Symbol::ChevronUp));detailToggle_->setToolTip(expanded?"Collapse details":"Expand details");if(const auto* e=selected())preview(e);});
    thumbnails_=previewNetwork?previewNetwork:new QNetworkAccessManager(this);
    connect(sourceLink_,&QPushButton::clicked,this,[this]{const auto url=sourceLink_->property("originalPage").toUrl();if(url.isValid()&&(url.scheme()=="http"||url.scheme()=="https")&&url.userInfo().isEmpty())QDesktopServices::openUrl(url);});
    tray_=new QSystemTrayIcon(windowIcon(),this);tray_->setObjectName("systemTray");tray_->setToolTip("Downshift");auto* trayMenu=new QMenu(this);trayMenu->setObjectName("trayMenu");trayMenu->addAction("Show Downshift",this,&MainWindow::showDownloads);trayMenu->addSeparator();auto* exit=trayMenu->addAction("Exit Downshift",this,&MainWindow::requestExit);exit->setObjectName("trayExit");tray_->setContextMenu(trayMenu);
    connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::Trigger||r==QSystemTrayIcon::DoubleClick)showDownloads();});
    theme(current_.settings.dark);updateNavigationLayout();details();
    const auto shortcut=[&](const QKeySequence& keys,const std::function<void()>& action){auto* key=new QShortcut(keys,this);connect(key,&QShortcut::activated,this,action);};shortcut(QKeySequence("Ctrl+N"),[this]{if(page_==4)openTorrent();else openAdd();});shortcut(QKeySequence("Ctrl+F"),[this]{search_->setFocus();search_->selectAll();});shortcut(QKeySequence("Ctrl+,"),[this]{openSettings();});shortcut(QKeySequence("Escape"),[this]{clearDownloadSelection();});
    auto* timer=new QTimer(this); timer->setInterval(250); connect(timer,&QTimer::timeout,this,&MainWindow::refresh); timer->start(); refresh();
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()==QEvent::MouseButtonPress){
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton&&mouse->modifiers()==Qt::NoModifier
            &&(watched!=table_->viewport()||!table_->indexAt(mouse->position().toPoint()).isValid()))
            clearDownloadSelection();
    }
    return QMainWindow::eventFilter(watched,event);
}
void MainWindow::clearDownloadSelection() {
    auto* selection=table_->selectionModel();
    if(!selection->hasSelection()&&!selection->currentIndex().isValid())return;
    const QSignalBlocker blocked(table_);
    selection->clear();
    details();
}
void MainWindow::refresh() {
    if(torrentClosing_)return;
    if(pending_) {
        if(pending_->wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        try { apply(pending_->get()); } catch(const std::exception& e) {message(QString::fromUtf8(e.what()),true);add_->setEnabled(false);video_->setEnabled(false);emptyAdd_->hide();start_->setEnabled(false);pause_->setEnabled(false);cancel_->setEnabled(false);if(!ready_){retryHistory_->show();retryHistory_->setEnabled(true);empty_->setText("History could not be loaded");emptyHelp_->setText("Your history and files are preserved. Close any other Downshift window, then retry.");}}
        pending_.reset();
    }
    pending_.emplace(queue_.snapshot());
}
void MainWindow::apply(const QueueSnapshot& snapshot) {
    const bool keep=!exitRequested_&&snapshot.settings.closeToTray&&QSystemTrayIcon::isSystemTrayAvailable();QApplication::setQuitOnLastWindowClosed(!keep);
    tray_->setVisible(QSystemTrayIcon::isSystemTrayAvailable()&&!testAttribute(Qt::WA_DontShowOnScreen)&&!QApplication::arguments().contains("--smoke-test"));
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
    current_=snapshot;ready_=true;retryHistory_->hide();const QSignalBlocker blocked(table_);
    displayOrder_.clear();for(std::size_t n=0;n<current_.entries.size();++n)displayOrder_.push_back({false,n});for(std::size_t n=0;n<torrentRows_.size();++n)displayOrder_.push_back({true,n});
    std::sort(displayOrder_.begin(),displayOrder_.end(),[this](auto a,auto b){
        const auto size=[this](auto row)->quint64{return row.torrent?static_cast<quint64>(torrentRows_[row.index].selectedBytes):current_.entries[row.index].progress.totalBytes.value_or(0);};
        const auto created=[this](auto row){return row.torrent?torrentRows_[row.index].created:current_.entries[row.index].created;};
        if(view_->currentIndex()==2&&size(a)!=size(b))return size(a)>size(b);
        if(created(a)!=created(b))return view_->currentIndex()==1?created(a)<created(b):created(a)>created(b);
        if(a.torrent!=b.torrent)return a.torrent;
        return view_->currentIndex()==1?a.index<b.index:a.index>b.index;
    });
    const auto keyFor=[this](DisplayRow row){return rowKey(row.torrent?torrentRows_[row.index].id:current_.entries[row.index].id,row.torrent);};
    const auto count=static_cast<int>(displayOrder_.size());
    bool orderChanged=table_->rowCount()!=count;
    for(int row=0;row<count&&!orderChanged;++row)
        orderChanged=rowKey(table_->item(row,0))!=keyFor(displayOrder_[row]);
    QSet<QString> selection;
    QString currentKey;
    if(orderChanged){
        for(int row:selectedRows())selection.insert(rowKey(table_->item(row,0)));
        currentKey=rowKey(table_->item(activeSelectedRow(),0));
        table_->clearSelection();table_->setCurrentCell(-1,-1);table_->setRowCount(count);
    }
    int running=0;double speed=0;std::uint64_t downloaded=0;table_->setUpdatesEnabled(false);
    for(int row=0;row<table_->rowCount();++row) {
        const auto display=displayOrder_[static_cast<std::size_t>(row)];
        if(display.torrent){const auto& entry=torrentRows_[display.index];renderTorrentRow(row,entry);if(torrentActive(entry)){++running;speed+=entry.downloadBytesPerSecond;}downloaded+=static_cast<std::uint64_t>(entry.verifiedBytes);continue;}
        const auto& e=current_.entries[display.index]; const auto& p=e.progress; const bool active=e.status==QueueStatus::Running;
        if(active){++running;speed+=p.smoothedBytesPerSecond;}
        downloaded+=p.downloadedBytes;
        const auto percent=e.status==QueueStatus::Completed ? 100.0:p.totalBytes && *p.totalBytes ? std::min(e.media && active ? 99.0:100.0,100.0*static_cast<double>(p.downloadedBytes)/static_cast<double>(*p.totalBytes)) : 0;
        const QStringList text={e.media && e.status!=QueueStatus::Completed ? e.mediaTitle:QFileInfo(e.destination).fileName(),p.totalBytes ? bytes(*p.totalBytes):"—",bytes(p.downloadedBytes),"",active ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",active && p.etaSeconds ? transferDuration(*p.etaSeconds):"—",active ? QString::number(p.activeRanges):"—",statusText(e.status),e.media ? "Video":e.priority>0 ? "High":e.priority<0 ? "Low":"Normal"};
        for(int col=0;col<11;++col) {
            auto* item=table_->item(row,col); if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);} if(item->text()!=(col<9?text[col]:col==9?QDateTime::fromSecsSinceEpoch(e.created).toString("dd MMM"):QString())) item->setText(col<9?text[col]:col==9?QDateTime::fromSecsSinceEpoch(e.created).toString("dd MMM"):QString());
            if(col==0){
                const auto source=QUrl(e.url).host();const auto type=e.media?QString("Video"):QString("HTTP");
                const auto metadata=source.isEmpty()?type:type+" · "+source;
                item->setData(Qt::UserRole,e.id);item->setData(Qt::UserRole+1,metadata);item->setData(Qt::UserRole+10,false);
                item->setData(Qt::AccessibleDescriptionRole,metadata);
                item->setToolTip(QString("<qt>%1<br>%2<br>%3</qt>").arg(text[0].toHtmlEscaped(),metadata.toHtmlEscaped(),e.destination.toHtmlEscaped()));
                const auto category=categoryFor(e);
                auto symbol=fileNavigation[static_cast<int>(category)].symbol;
                if(category==FileCategory::Program&&!QStringList{"exe","msi","msix"}.contains(QFileInfo(e.destination).suffix().toLower()))symbol=Symbol::File;
                item->setIcon(lineIcon(symbol,fileIconColor(symbol,current_.settings.dark)));
            }
            if(col==3){
                const bool unknown=!p.totalBytes&&e.status!=QueueStatus::Completed;
                item->setData(Qt::UserRole+3,static_cast<int>(e.status));item->setData(Qt::UserRole+1,percent);item->setData(Qt::UserRole+2,unknown);
                item->setData(Qt::AccessibleTextRole,unknown?QString("Progress unavailable"):QString::number(percent,'f',0)+"% complete");
                item->setToolTip(bytes(p.downloadedBytes)+" downloaded");
            }
            if(col==7)item->setData(Qt::UserRole+1,static_cast<int>(e.status));
            if(col==9)item->setToolTip(QDateTime::fromSecsSinceEpoch(e.created).toString(Qt::ISODate));
        }
    }
    if(!selection.isEmpty()){
        auto* model=table_->selectionModel();
        QItemSelection restored;
        QModelIndex current;
        for(int row=0;row<count;++row){
            const auto key=rowKey(table_->item(row,0));
            if(!selection.contains(key))continue;
            const auto index=table_->model()->index(row,0);
            restored.select(index,index);
            if(key==currentKey)current=index;
        }
        model->select(restored,QItemSelectionModel::Select|QItemSelectionModel::Rows);
        model->setCurrentIndex(current,QItemSelectionModel::NoUpdate);
    }
    totalDownloaded_=downloaded;
    totalMetric_->setText(QString::number(table_->rowCount()));activeMetric_->setText(QString::number(running));speed_->setText(bytes(static_cast<std::uint64_t>(speed))+"/s");add_->setEnabled(true);video_->setEnabled(true);emptyAdd_->setEnabled(true);filter();table_->setUpdatesEnabled(true);if(!snapshot.error.isEmpty())message(snapshot.error,true);
    table_->verticalScrollBar()->setValue(scrollPosition);
}
const QueueEntry* MainWindow::selected() const {
    const auto row=activeSelectedRow(); if(row<0 || !table_->item(row,0)) return nullptr;
    if(table_->item(row,0)->data(Qt::UserRole+10).toBool())return nullptr;
    const auto id=table_->item(row,0)->data(Qt::UserRole).toString(); const auto i=std::find_if(current_.entries.begin(),current_.entries.end(),[&](const auto& e){return e.id==id;}); return i==current_.entries.end() ? nullptr : &*i;
}
QList<int> MainWindow::selectedRows() const {
    QList<int> rows;
    for(const auto& index:table_->selectionModel()->selectedRows())
        if(!table_->isRowHidden(index.row()))rows.append(index.row());
    return rows;
}
int MainWindow::activeSelectedRow() const {
    const auto row=table_->currentRow();
    if(row>=0&&!table_->isRowHidden(row)&&table_->selectionModel()->isRowSelected(row,{}))return row;
    const auto rows=selectedRows();
    return rows.isEmpty()?-1:rows.front();
}
void MainWindow::updateDownloadedTotal(const QList<int>& rows) {
    if(!ready_)return;
    auto total=rows.isEmpty()?totalDownloaded_:0;
    for(int row:rows){
        const auto display=displayOrder_[row];
        total+=display.torrent?static_cast<quint64>(torrentRows_[display.index].verifiedBytes)
            :current_.entries[display.index].progress.downloadedBytes;
    }
    dataMetric_->setText(bytes(total));
    dataMetric_->setToolTip(rows.isEmpty()
        ?QString("Bytes downloaded across all downloads, including retained partials.")
        :QString("Bytes downloaded across %1 selected download(s).").arg(rows.size()));
}
void MainWindow::filter() {
    if(page_==4){int active=0;quint64 rate=0;for(const auto& row:torrentRows_){if(torrentActive(row))++active;rate+=static_cast<quint64>(row.downloadBytesPerSecond);}totalMetric_->setText(QString::number(torrentRows_.size()));activeMetric_->setText(QString::number(active));speed_->setText(bytes(rate)+"/s");}
    std::array<int,4> statusCounts{};
    const auto text=search_->text();
    int visible=0;
    for(int row=0;row<table_->rowCount();++row) {
        const auto display=displayOrder_[static_cast<std::size_t>(row)];
        int category=4;
        bool inProgress=false,finished=false,needsAttention=false,match=false;
        if(display.torrent){
            const auto& e=torrentRows_[display.index];
            inProgress=torrentActive(e)||e.state==torrent::State::Paused;
            finished=e.state==torrent::State::Complete||e.state==torrent::State::Seeding;
            needsAttention=e.state==torrent::State::Failed||e.state==torrent::State::Cancelled;
            match=e.name.contains(text,Qt::CaseInsensitive)||e.directory.contains(text,Qt::CaseInsensitive)||QString("Torrent").contains(text,Qt::CaseInsensitive);
        }else{
            const auto& e=current_.entries[display.index];
            inProgress=e.status==QueueStatus::Running||e.status==QueueStatus::Queued||e.status==QueueStatus::Paused;
            finished=e.status==QueueStatus::Completed;
            needsAttention=e.status==QueueStatus::Failed||e.status==QueueStatus::Cancelled;
            if(page_>=firstFilePage)category=filePage(categoryFor(e));
            else category=-1;
            match=e.destination.contains(text,Qt::CaseInsensitive)||(e.media&&e.mediaTitle.contains(text,Qt::CaseInsensitive))
                ||e.url.contains(text,Qt::CaseInsensitive)||(e.media?QString("Video"):QString("HTTP")).contains(text,Qt::CaseInsensitive);
        }
        statusCounts[1]+=inProgress;statusCounts[2]+=finished;statusCounts[3]+=needsAttention;
        const bool show=match&&(page_==0||page_==category||(page_==1&&inProgress)
            ||(page_==2&&finished)||(page_==3&&needsAttention));
        table_->setRowHidden(row,!show);
        if(show)++visible;
    }
    auto* selection=table_->selectionModel();
    QItemSelection hidden;
    for(const auto& index:selection->selectedRows())
        if(table_->isRowHidden(index.row()))hidden.select(index,index);
    if(!hidden.isEmpty()){
        const QSignalBlocker blocked(table_);
        selection->select(hidden,QItemSelectionModel::Deselect|QItemSelectionModel::Rows);
    }
    const auto tokens=DesignTokens::theme(current_.settings.dark);
    for(auto* button:findChildren<QPushButton*>("navigation")){
        const auto page=button->property("navigationPage").toInt();
        if(auto* count=button->findChild<QLabel*>("navigationCount"))count->setText(QString::number(statusCounts[page]));
        button->setChecked(page==page_);
        const auto symbol=static_cast<Symbol>(button->property("navigationSymbol").toInt());
        button->setIcon(lineIcon(symbol,page==page_?tokens.pink:tokens.muted));
    }
    if(ready_){table_->setVisible(visible>0);emptyPanel_->setVisible(visible==0);empty_->setText((page_==4?torrentRows_.empty():table_->rowCount()==0) ? (page_==4?"Add your first torrent":"No downloads yet"):"No matching downloads");emptyHelp_->setText((page_==4?torrentRows_.empty():table_->rowCount()==0) ? (page_==4 ? "Add a torrent file or magnet link.":"Add a link to start your first download."):"Try another search or choose a different section.");emptyAdd_->show();}add_->setText(page_==4?"Add torrent":"Add URL");emptyAdd_->setText(page_==4?"Add torrent":"Add download");video_->hide();if(auto* settings=findChild<QPushButton*>("torrentSettingsButton"))settings->setVisible(page_==4);details();
}
void MainWindow::details() {
    const auto rows=selectedRows();
    updateDownloadedTotal(rows);
    if(rows.size()>1){
        detailsContainer_->hide();
        preview(nullptr);
        for(auto* button:{start_,pause_,cancel_})button->setEnabled(false);
        more_->setEnabled(true);
        return;
    }
    const auto color=DesignTokens::theme(current_.settings.dark).muted;
    detailToggle_->setIcon(lineIcon(detailToggle_->isChecked()?Symbol::Chevron:Symbol::ChevronUp,color));
    if(const auto* row=selectedTorrent()){
        start_->setText(row->state==torrent::State::Complete?"Completed":row->state==torrent::State::Seeding?"Seeding":row->verifyingCompletion?"Verifying":row->metadataOnly?(row->metadata?"Choose files":"Find metadata"):"Resume");start_->setEnabled(!torrentClosing_&&!torrentBusy_&&!row->verifyingCompletion&&row->state!=torrent::State::Complete&&row->state!=torrent::State::Cancelled&&(row->metadataOnly||(row->state!=torrent::State::Seeding&&row->state!=torrent::State::Downloading)));
        pause_->setEnabled(!torrentClosing_&&!torrentBusy_&&!row->verifyingCompletion&&torrentActive(*row));cancel_->setEnabled(!torrentClosing_&&!torrentBusy_&&row->state!=torrent::State::Cancelled&&row->state!=torrent::State::Complete&&row->state!=torrent::State::Seeding);more_->setEnabled(!torrentClosing_&&!torrentBusy_);if(!table_->property("detailsAllowed").toBool()&&(!detailsContainer_->isVisible()||detailsContainer_->property("downloadId").toString()!=row->id)){detailsContainer_->hide();return;}detailsContainer_->setProperty("downloadId",row->id);detailsContainer_->show();detailScroll_->setVisible(detailToggle_->isChecked());detailToggle_->setCheckable(true);detailToggle_->setText("");detailToggle_->setToolTip(detailToggle_->isChecked()?"Collapse details":"Expand details");torrentDetails(row->id);updateDetailHeight();
        preview(nullptr);detailTitle_->setText(detailTitle_->fontMetrics().elidedText(row->name,Qt::ElideRight,qMax(100,detailTitle_->width())));detailTitle_->setToolTip(row->name);sourceLink_->setText("Torrent · "+QString::number(row->files.size())+" files · "+torrentStateText(*row));sourceLink_->setEnabled(false);detailCopy_->setEnabled(false);detailAction_->setEnabled(start_->isEnabled()||pause_->isEnabled());detailAction_->setIcon(lineIcon(pause_->isEnabled()?Symbol::Pause:Symbol::Resume,color));detailAction_->setToolTip(row->state==torrent::State::Complete?"Completed":pause_->isEnabled()?"Pause":"Resume");
        const QStringList values={"Torrent",row->directory,bytes(static_cast<std::uint64_t>(row->selectedBytes)),bytes(static_cast<std::uint64_t>(row->verifiedBytes)),bytes(static_cast<std::uint64_t>(row->downloadBytesPerSecond))+"/s","↑ "+bytes(static_cast<std::uint64_t>(row->uploadBytesPerSecond))+"/s",QString("Ratio %1").arg(row->ratio,0,'f',2),row->downloadBytesPerSecond>0?transferDuration(static_cast<double>(row->selectedBytes-row->verifiedBytes)/row->downloadBytesPerSecond):QString("—"),QString("%1 peers / %2 seeds").arg(row->peers).arg(row->seeds),bytes(static_cast<std::uint64_t>(row->verifiedBytes)),torrentStateText(*row),row->error.isEmpty()?QString("None"):row->error};
        for(std::size_t i=0;i<detailLabels_.size();++i){detailLabels_[i].second->setText(values[static_cast<int>(i)]);detailLabels_[i].second->setToolTip(values[static_cast<int>(i)]);}
        return;
    }
    const auto* e=selected();MediaDialog* mediaTask=nullptr;if(e&&e->media)for(auto* child:findChildren<QDialog*>("mediaDialog"))if(child->property("mediaId").toString()==e->id)mediaTask=dynamic_cast<MediaDialog*>(child);const bool retained=e&&e->media&&e->status==QueueStatus::Paused&&validMediaResume(e->videoMetadata.value("resumeRecipe").toObject());start_->setText(e && ((e->media&&!retained&&e->status!=QueueStatus::Running&&e->status!=QueueStatus::Completed)||(!e->media&&!e->browserTransfer&&!youtubeVideo(e->url).isEmpty())) ? "Retry video":"Resume"); start_->setEnabled(e && e->status!=QueueStatus::Completed && e->status!=QueueStatus::Running && e->status!=QueueStatus::Prepared); pause_->setEnabled(e && (e->media ? mediaTask&&mediaTask->canPause():(e->status==QueueStatus::Running || e->status==QueueStatus::Queued))); cancel_->setEnabled(e && (e->media ? (e->status==QueueStatus::Running||e->status==QueueStatus::Paused):e->status!=QueueStatus::Completed && e->status!=QueueStatus::Cancelled && e->status!=QueueStatus::Prepared)); more_->setEnabled(e!=nullptr);
    if(e&&!table_->property("detailsAllowed").toBool()&&(!detailsContainer_->isVisible()||detailsContainer_->property("downloadId").toString()!=e->id)){detailsContainer_->hide();return;}
    if(e)detailsContainer_->setProperty("downloadId",e->id);
    torrentDetailsStack_->hide();detailTabs_->show();updateDetailHeight();detailToggle_->setCheckable(true);detailToggle_->setText("");detailToggle_->setToolTip(detailToggle_->isChecked()?"Collapse details":"Expand details");
    detailsContainer_->setVisible(e!=nullptr);preview(e);if(!e) {detailTitle_->setText("Select a download");for(auto& [name,label]:detailLabels_)label->setText("—");return;}
    detailScroll_->setVisible(detailToggle_->isChecked());
    const auto& p=e->progress; const auto title=e->media&&!e->mediaTitle.isEmpty()?e->mediaTitle:QFileInfo(e->destination).fileName();detailTitle_->setText(detailTitle_->fontMetrics().elidedText(title,Qt::ElideRight,qMax(100,detailTitle_->width())));detailTitle_->setToolTip(title);
    detailCopy_->setEnabled(true);detailAction_->setEnabled(e->status==QueueStatus::Completed||start_->isEnabled()||pause_->isEnabled());detailAction_->setIcon(lineIcon(e->status==QueueStatus::Completed?Symbol::Program:pause_->isEnabled()?Symbol::Pause:Symbol::Resume,color));detailAction_->setToolTip(e->status==QueueStatus::Completed?"Open file…":pause_->isEnabled()?"Pause":"Resume or retry");
    detailTabs_->setTabVisible(1,!e->media);detailTabs_->setTabText(1,"Connections");connectionInfo_->setText(QString("%1 active / %2 target connections\n%3 verified\n%4").arg(p.activeRanges).arg(p.targetConnections).arg(bytes(p.verifiedBytes),p.resumable?"Range checkpoints are verified before reuse.":"This source requires a fresh transfer when it cannot resume."));taskLog_->setText(statusText(e->status)+" · Added "+QDateTime::fromSecsSinceEpoch(e->created).toString(Qt::ISODate)+(e->error.isEmpty()?QString():"\n"+e->error));
    const QStringList text={QUrl(e->url).adjusted(QUrl::RemoveQuery|QUrl::RemoveFragment|QUrl::RemoveUserInfo).toDisplayString(),e->destination,p.totalBytes ? bytes(*p.totalBytes):"Unknown",bytes(p.downloadedBytes),e->status==QueueStatus::Running ? bytes(static_cast<std::uint64_t>(p.smoothedBytesPerSecond))+"/s":"—",bytes(static_cast<std::uint64_t>(p.averageBytesPerSecond))+"/s",transferDuration(p.elapsedSeconds),p.etaSeconds ? transferDuration(*p.etaSeconds):"—",QString("%1 active / %2 target").arg(p.activeRanges).arg(p.targetConnections),bytes(p.verifiedBytes),p.resumable ? "Supported; verified before reuse":"Requires server validation; some files need Restart",e->error.isEmpty() ? "None":e->error};
    for(std::size_t i=0;i<detailLabels_.size();++i){const auto value=e->media && i==10 ? (retained||mediaTask&&mediaTask->canPause()?QString("Pause retains partial files; Resume continues this video."):QString("Retry starts a fresh transfer after failure or cancellation.")):text[static_cast<int>(i)];detailLabels_[i].second->setText(value);detailLabels_[i].second->setToolTip(value);}
}
void MainWindow::message(const QString& text,bool error) {static_cast<Notification*>(banner_)->present(text,error?Notice::Critical:Notice::Success);}
void MainWindow::watchTorrent(std::future<void> future){
    torrentBusy_=true;details();auto pending=std::make_shared<std::future<void>>(std::move(future));auto* timer=new QTimer(this);timer->setInterval(30);
    connect(timer,&QTimer::timeout,this,[this,timer,pending]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();torrentBusy_=false;try{pending->get();banner_->hide();}catch(const std::exception& error){message(QString::fromUtf8(error.what()),true);}details();refresh();});timer->start();
}
void MainWindow::perform(QueueAction action) {
    if(const auto* row=selectedTorrent()){
        const auto id=row->id;if(action==QueueAction::Start){if(row->state==torrent::State::Complete||row->verifyingCompletion)return;if(row->metadataOnly&&row->metadata){detailToggle_->setChecked(true);torrentDetails(id);return;}watchTorrent(torrents_->resume(id));}
        else if(action==QueueAction::Pause)watchTorrent(torrents_->pause(id));else if(action==QueueAction::Cancel)watchTorrent(torrents_->cancel(id));return;
    }
    if(const auto* e=selected()) {
    if(!e->media && !e->browserTransfer && !youtubeVideo(e->url).isEmpty() && action==QueueAction::Start){openVideo(e->url,"YouTube video");return;}
    if(e->media){if(action==QueueAction::Start){if(e->status==QueueStatus::Paused)resumeVideo(*e);else openVideo(e->url,e->mediaTitle,e->mediaFolder,e->videoMetadata.value("requestContext").toObject(),e->videoMetadata.value("downloadName").toString());}else {bool found=false;for(auto* dialog:findChildren<QDialog*>("mediaDialog"))if(dialog->property("mediaId").toString()==e->id)if(auto* media=dynamic_cast<MediaDialog*>(dialog)){found=true;if(action==QueueAction::Pause)media->pause();else if(action==QueueAction::Cancel)media->stop();}if(!found&&action==QueueAction::Cancel&&e->status==QueueStatus::Paused)watch(queue_.finishMedia(e->id,QueueStatus::Cancelled,{},"Cancelled. Incomplete video files removed."),[](const QString&){});}return;}
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
QPushButton* MainWindow::navigationButton(const QString& title,int page,Symbol symbol) {
    const bool compact=sidebar_->property("compact").toBool();
    auto* button=makeButton(compact?QString():title,"navigation",sidebar_);
    button->setProperty("navigationTitle",title);button->setProperty("navigationPage",page);
    button->setProperty("navigationSymbol",static_cast<int>(symbol));button->setProperty("categoryNav",page>=4);
    button->setAccessibleName(title);button->setToolTip(title);button->setIconSize(QSize(16,16));
    button->setCheckable(true);button->setChecked(page==page_);
    if(page>0&&page<4){
        button->setProperty("navCount",true);
        auto* count=new QLabel(button);count->setObjectName("navigationCount");count->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        count->setFixedWidth(32);count->setAttribute(Qt::WA_TransparentForMouseEvents);count->setVisible(!compact);
        auto* countLayout=new QHBoxLayout(button);countLayout->setContentsMargins(0,0,6,0);countLayout->addStretch();countLayout->addWidget(count);
    }
    connect(button,&QPushButton::clicked,this,[this,page]{page_=page;if(ready_)apply(current_);else filter();table_->scrollToTop();});
    return button;
}
void MainWindow::updateNavigationLayout() {
    if(!sidebar_)return;
    const bool compact=width()<920;
    if(sidebar_->property("compact").isValid()&&sidebar_->property("compact").toBool()==compact)return;
    sidebar_->setFixedWidth(compact?56:184);sidebar_->setProperty("compact",compact);
    categoryLayout_->setContentsMargins(compact?0:18,2,0,3);
    categoryToggle_->setVisible(!compact);
    categoryNav_->setVisible(compact||categoryToggle_->isChecked());
    for(auto* button:findChildren<QPushButton*>("navigation")){
        button->setText(compact?QString():button->property("navigationTitle").toString());
        if(auto* count=button->findChild<QLabel*>("navigationCount"))count->setVisible(!compact);
    }
    for(const auto& [name,title]:{std::pair{"settingsButton","Settings"},std::pair{"torrentSettingsButton","Torrent limits"}})
        if(auto* button=findChild<QPushButton*>(name)){button->setText(compact?QString():QString(title));button->setAccessibleName(title);button->setToolTip(title);}
    for(auto* button:sidebar_->findChildren<QPushButton*>()){button->style()->unpolish(button);button->style()->polish(button);}
}
void MainWindow::updateDetailHeight() {
    if(!detailScroll_)return;
    const int maximum=torrentDetailsStack_->isHidden()?168:220;
    detailScroll_->setMaximumHeight(qBound(80,height()-460,maximum));
}
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateNavigationLayout();updateDetailHeight();
}
void MainWindow::theme(bool dark) {
    applyDesign(dark);const auto tokens=DesignTokens::theme(dark);const auto color=tokens.muted;
    table_->setColumnWidth(7,DownloadDelegate::statusColumnWidth(table_->font()));static_cast<DownloadTable*>(table_)->fitColumns();
    categoryToggle_->setIcon(lineIcon(categoryToggle_->isChecked()?Symbol::ChevronUp:Symbol::Chevron,color));
    for(auto* button:findChildren<QPushButton*>("navigation")){
        const auto symbol=static_cast<Symbol>(button->property("navigationSymbol").toInt());
        button->setIcon(lineIcon(symbol,button->isChecked()?tokens.pink:color));
    }
    add_->setIcon(lineIcon(Symbol::Plus,Qt::white));emptyAdd_->setIcon(lineIcon(Symbol::Plus,Qt::white));video_->setIcon(lineIcon(Symbol::Video,color));start_->setIcon(lineIcon(Symbol::Resume,color));pause_->setIcon(lineIcon(Symbol::Pause,color));cancel_->setIcon(lineIcon(Symbol::Cancel,tokens.error));more_->setIcon(lineIcon(Symbol::More,color));detailToggle_->setIcon(lineIcon(detailToggle_->isChecked()?Symbol::Chevron:Symbol::ChevronUp,color));
    for(const auto* name:{"settingsButton","torrentSettingsButton"})if(auto* button=findChild<QPushButton*>(name))button->setIcon(lineIcon(Symbol::Settings,color));
    detailFolder_->setIcon(lineIcon(Symbol::Folder,color));detailCopy_->setIcon(lineIcon(Symbol::Copy,color));
    sourceLink_->setIcon(lineIcon(Symbol::Link,tokens.pink));
    if(auto* close=findChild<QPushButton*>("closeDetails"))close->setIcon(lineIcon(Symbol::Cancel,color));
    if(!search_->actions().isEmpty())search_->actions().first()->setIcon(lineIcon(Symbol::Search,color));
}
}
