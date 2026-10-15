#include "TorrentDialogs.h"
#include "MainWindow.h"
#include "Controls.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QDir>
#include <QUuid>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <optional>

namespace downshift {
using namespace torrent;
QString torrentStateText(const Snapshot& s){
    if(s.metadataOnly&&s.metadata&&s.state!=State::Cancelled&&s.state!=State::Failed)return "Choose files";
    switch(s.state){case State::Adding:return "Adding";case State::Metadata:return "Finding metadata";case State::Checking:return "Verifying";case State::Downloading:return "Downloading";case State::Complete:return "Completed";case State::Failed:return "Failed";case State::Cancelled:return "Cancelled";case State::Paused:return "Paused";case State::Seeding:return "Seeding";}return "Unknown";
}
bool torrentActive(const Snapshot& s){return s.state==State::Adding||s.state==State::Metadata||s.state==State::Checking||s.state==State::Downloading||s.state==State::Seeding;}
namespace {
QString amount(qint64 n){double value=static_cast<double>(n);int unit=0;const QStringList units={"B","KiB","MiB","GiB","TiB"};while(value>=1024&&unit<4){value/=1024;++unit;}return QString::number(value,'f',unit?1:0)+" "+units[unit];}
QLabel* plain(const QString& text,QWidget* parent=nullptr){auto* label=new QLabel(text,parent);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse);return label;}
class Dialog:public QDialog {
protected:
    bool busy_=false;
    QLabel* error_=nullptr;
    template<class T,class F>void await(std::future<T> future,F success){
        auto pending=std::make_shared<std::future<T>>(std::move(future));auto* timer=new QTimer(this);timer->setInterval(25);
        connect(timer,&QTimer::timeout,this,[this,timer,pending,success=std::move(success)]()mutable{
            if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            timer->stop();timer->deleteLater();busy_=false;
            try{if constexpr(std::is_void_v<T>){pending->get();success();}else success(pending->get());}
            catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));}
        });timer->start();
    }
    QLabel* error(QVBoxLayout* layout){error_=plain({});error_->setObjectName("torrentError");layout->addWidget(error_);return error_;}
public:
    explicit Dialog(QWidget* parent):QDialog(parent){setAttribute(Qt::WA_DeleteOnClose);setMinimumSize(560,420);resize(740,570);}
};
class FileTable:public QTableWidget {
    QStringList paths_;
public:
    explicit FileTable(QWidget* parent=nullptr):QTableWidget(0,4,parent){setObjectName("torrentFiles");setHorizontalHeaderLabels({"File","Size","Verified","Priority"});setEditTriggers(QAbstractItemView::NoEditTriggers);horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);setColumnWidth(3,140);verticalHeader()->hide();verticalHeader()->setDefaultSectionSize(36);}
    void updateFiles(const std::vector<File>& files){
        QStringList paths;for(const auto& f:files)paths.append(f.path);
        const bool rebuild=paths_!=paths;if(rebuild){paths_=paths;setRowCount(static_cast<int>(files.size()));}
        setUpdatesEnabled(false);
        for(int n=0;n<static_cast<int>(files.size());++n){const auto& file=files[static_cast<std::size_t>(n)];
            const QStringList values={file.path,amount(file.bytes),amount(file.verifiedBytes)};
            for(int col=0;col<3;++col){if(!item(n,col))setItem(n,col,new QTableWidgetItem);item(n,col)->setText(values[col]);item(n,col)->setToolTip(values[col]);}
            if(rebuild){auto* priority=new QComboBox;priority->setObjectName("torrentFilePriority");priority->addItems({"Skip","1 · Low","2","3","4 · Normal","5","6","7 · High"});priority->setCurrentIndex(file.priority);modernCombo(priority);setCellWidget(n,3,priority);}
        }setUpdatesEnabled(true);
    }
    std::vector<int> priorities() const{std::vector<int> result;for(int n=0;n<rowCount();++n)result.push_back(qobject_cast<QComboBox*>(cellWidget(n,3))->currentIndex());return result;}
};
class RateField final:public QDoubleSpinBox {
    QString textFromValue(double value)const override{return QString::number(value,'g',17);}
};
struct RateForm {
    QDoubleSpinBox *download=nullptr,*upload=nullptr;QSpinBox* connections=nullptr;
    explicit RateForm(QFormLayout* form,const QString& prefix){
        const auto rate=[&](const QString& key){auto* field=new RateField;field->setRange(0,2097151.999);field->setDecimals(6);field->setButtonSymbols(QAbstractSpinBox::NoButtons);field->setSuffix(" KiB/s");field->setSpecialValueText("Unlimited");field->setObjectName(prefix+key);return field;};
        download=rate("Download");upload=rate("Upload");connections=new QSpinBox;connections->setButtonSymbols(QAbstractSpinBox::NoButtons);connections->setRange(1,512);connections->setObjectName(prefix+"Connections");form->addRow("Download limit",download);form->addRow("Upload limit",upload);form->addRow("Peer connections",connections);
    }
    void set(Limits limits){download->setValue(limits.download/1024.);upload->setValue(limits.upload/1024.);connections->setValue(limits.connections);}
    Limits get()const{return {static_cast<int>(std::llround(download->value()*1024)),static_cast<int>(std::llround(upload->value()*1024)),connections->value()};}
};
struct SeedForm {
    QComboBox* mode=nullptr;QLineEdit* ratio=nullptr;QSpinBox* seconds=nullptr;
    explicit SeedForm(QFormLayout* form){mode=new QComboBox;mode->setObjectName("torrentSeedMode");mode->addItems({"Stop after download","Stop at upload ratio","Stop after seeding time","Continue until I stop"});modernCombo(mode);ratio=new QLineEdit;ratio->setObjectName("torrentSeedRatio");auto* validator=new QDoubleValidator(0,1000,1000,ratio);validator->setLocale(QLocale::c());ratio->setValidator(validator);seconds=new QSpinBox;seconds->setButtonSymbols(QAbstractSpinBox::NoButtons);seconds->setObjectName("torrentSeedSeconds");seconds->setRange(1,31*86400);seconds->setSuffix(" s");form->addRow("Seeding",mode);form->addRow("Target ratio",ratio);form->addRow("Seeding time",seconds);QObject::connect(mode,&QComboBox::currentIndexChanged,mode,[this]{ratio->setEnabled(mode->currentIndex()==1);seconds->setEnabled(mode->currentIndex()==2);});set({});}
    void set(SeedPolicy p){mode->setCurrentIndex(static_cast<int>(p.mode));ratio->setText(QString::number(p.ratio,'g',17));seconds->setValue(p.seconds);ratio->setEnabled(p.mode==SeedMode::Ratio);seconds->setEnabled(p.mode==SeedMode::Time);}
    SeedPolicy get()const{bool ok=false;const auto value=ratio->text().toDouble(&ok);if(!ok||!std::isfinite(value)||value<=0||value>1000)throw std::runtime_error("Enter a seeding ratio above zero and no greater than 1000.");return {static_cast<SeedMode>(mode->currentIndex()),value,seconds->value()};}
};
class AddDialog final:public Dialog {
    TorrentEngine& engine_;QString id_;QLineEdit *source_,*folder_;FileTable* files_;QLabel* status_;QPushButton *inspect_,*confirm_;QCheckBox* start_;
    std::optional<std::future<std::vector<Snapshot>>> polling_;
    void poll(){
        if(id_.isEmpty())return;
        if(polling_&&polling_->wait_for(std::chrono::seconds(0))==std::future_status::ready){try{for(const auto& row:polling_->get())if(row.id==id_){files_->updateFiles(row.files);status_->setText(row.metadata?"Choose files and priorities. No payload downloads until you confirm.":"Finding metadata from peers… You can cancel while waiting.");confirm_->setEnabled(row.metadata&&!busy_);if(!row.error.isEmpty())error_->setText(row.error);}}catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));}polling_.reset();}
        if(!polling_)polling_.emplace(engine_.snapshots());
    }
public:
    AddDialog(TorrentEngine& engine,QString folder,QWidget* parent):Dialog(parent),engine_(engine){
        setObjectName("addTorrentDialog");setWindowTitle("Add torrent");auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;layout->addLayout(form);
        source_=new QLineEdit;source_->setObjectName("torrentSource");source_->setPlaceholderText("Magnet link or .torrent file");auto* sourceLine=new QHBoxLayout;sourceLine->addWidget(source_);auto* browse=makeButton("Browse…","torrentBrowse");sourceLine->addWidget(browse);form->addRow("Torrent",sourceLine);
        folder_=new QLineEdit(QDir(folder).filePath("Torrents/"+QUuid::createUuid().toString(QUuid::Id128).left(8)));folder_->setObjectName("torrentFolder");auto* folderLine=new QHBoxLayout;folderLine->addWidget(folder_);auto* choose=makeButton("Choose…","torrentChooseFolder");folderLine->addWidget(choose);form->addRow("Save in",folderLine);
        connect(browse,&QPushButton::clicked,this,[this]{if(!id_.isEmpty()||busy_)return;const auto path=QFileDialog::getOpenFileName(this,"Choose torrent",{},"Torrents (*.torrent)");if(!path.isEmpty())source_->setText(path);});
        connect(choose,&QPushButton::clicked,this,[this]{if(!id_.isEmpty()||busy_)return;const auto path=QFileDialog::getExistingDirectory(this,"Torrent destination",folder_->text());if(!path.isEmpty())folder_->setText(path);});
        status_=plain("Read the metadata to choose files before downloading.");status_->setObjectName("torrentMetadataStatus");layout->addWidget(status_);files_=new FileTable;layout->addWidget(files_,1);error(layout);
        start_=new QCheckBox("Start downloading after adding");start_->setObjectName("torrentStart");start_->setChecked(false);layout->addWidget(start_);
        auto* footer=new QHBoxLayout;inspect_=makeButton("Read metadata","torrentInspect");confirm_=makeButton("Add paused","torrentConfirm");confirm_->setProperty("primary",true);confirm_->setEnabled(false);auto* cancel=makeButton("Cancel","torrentAddCancel");footer->addWidget(inspect_);footer->addStretch();footer->addWidget(cancel);footer->addWidget(confirm_);layout->addLayout(footer);
        connect(start_,&QCheckBox::toggled,this,[this](bool start){confirm_->setText(start?"Start download":"Add paused");});connect(cancel,&QPushButton::clicked,this,&AddDialog::reject);
        connect(inspect_,&QPushButton::clicked,this,[this]{if(busy_||!id_.isEmpty())return;busy_=true;error_->clear();inspect_->setEnabled(false);source_->setEnabled(false);folder_->setEnabled(false);const auto value=source_->text().trimmed();const bool magnet=value.startsWith("magnet:",Qt::CaseInsensitive);AddOptions options;options.metadataOnly=true;options.paused=!magnet;await(magnet?engine_.addMagnet(value,folder_->text().trimmed(),options):engine_.addFile(value,folder_->text().trimmed(),options),[this](QString id){id_=std::move(id);poll();});});
        connect(confirm_,&QPushButton::clicked,this,[this]{if(busy_||id_.isEmpty())return;busy_=true;confirm_->setEnabled(false);error_->clear();await(engine_.selectFiles(id_,files_->priorities()),[this]{busy_=true;await(engine_.startSelected(id_,!start_->isChecked()),[this]{id_.clear();accept();});});});
        auto* timer=new QTimer(this);timer->setInterval(250);connect(timer,&QTimer::timeout,this,[this]{if(id_.isEmpty()&&!busy_){inspect_->setEnabled(true);source_->setEnabled(true);folder_->setEnabled(true);}poll();});timer->start();
    }
    void reject()override{if(busy_)return;if(id_.isEmpty()){QDialog::reject();return;}busy_=true;confirm_->setEnabled(false);await(engine_.remove(id_),[this]{id_.clear();QDialog::reject();});}
};
class DetailsDialog final:public Dialog {
    TorrentEngine& engine_;QString id_;Snapshot snapshot_;FileTable* files_;QLabel* overview_;QTableWidget *trackers_,*peers_;QPushButton *apply_,*confirm_;QCheckBox* start_;QFormLayout* settingsForm_=new QFormLayout;RateForm rates_;SeedForm seeds_;
    std::optional<std::future<std::vector<Snapshot>>> polling_;std::optional<std::future<Details>> details_;
    void poll(){
        if(polling_&&polling_->wait_for(std::chrono::seconds(0))==std::future_status::ready){try{bool found=false;for(const auto& row:polling_->get())if(row.id==id_){found=true;const bool first=snapshot_.id.isEmpty();snapshot_=row;if(first){rates_.set(row.limits);seeds_.set(row.seeding);}files_->updateFiles(row.files);overview_->setText(QString("%1\n%2\n%3\nVerified %4 / %5 selected\nDownload %6/s · Upload %7/s\nSeeds %8 · Peers %9 · Ratio %10\nAvailability %11 · Seeding %12 s\n%13").arg(row.name,torrentStateText(row),row.directory,amount(row.verifiedBytes),amount(row.selectedBytes),amount(row.downloadBytesPerSecond),amount(row.uploadBytesPerSecond)).arg(row.seeds).arg(row.peers).arg(row.ratio,0,'f',3).arg(row.availability,0,'f',2).arg(row.seedingSeconds).arg(row.error));confirm_->setVisible(row.metadataOnly);start_->setVisible(row.metadataOnly);confirm_->setEnabled(row.metadata&&!busy_);apply_->setEnabled(row.metadata&&row.state!=State::Cancelled&&!busy_);}if(!found){error_->setText("This torrent was removed.");apply_->setEnabled(false);confirm_->setEnabled(false);}}catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));}polling_.reset();}
        if(details_&&details_->wait_for(std::chrono::seconds(0))==std::future_status::ready){try{const auto d=details_->get();trackers_->setRowCount(static_cast<int>(d.trackers.size()));for(int n=0;n<static_cast<int>(d.trackers.size());++n){const auto& t=d.trackers[static_cast<std::size_t>(n)];const auto url=QUrl(t.url);const QStringList values={url.host().isEmpty()?QString("Tracker"):url.host(),t.verified?"Verified":"Waiting",QString::number(t.failures),t.error};for(int c=0;c<4;++c)trackers_->setItem(n,c,new QTableWidgetItem(values[c]));}peers_->setRowCount(static_cast<int>(d.peers.size()));for(int n=0;n<static_cast<int>(d.peers.size());++n){const auto& p=d.peers[static_cast<std::size_t>(n)];const QStringList values={p.address,p.client,amount(p.download)+"/s",amount(p.upload)+"/s",p.seed?"Seed":"Peer"};for(int c=0;c<5;++c)peers_->setItem(n,c,new QTableWidgetItem(values[c]));}}catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));}details_.reset();}
        if(!polling_)polling_.emplace(engine_.snapshots());if(!details_)details_.emplace(engine_.details(id_));
    }
    void save(bool finish){if(busy_||snapshot_.id.isEmpty())return;try{const auto limits=rates_.get();const auto policy=seeds_.get();busy_=true;error_->clear();apply_->setEnabled(false);confirm_->setEnabled(false);await(engine_.selectFiles(id_,files_->priorities()),[this,finish,limits,policy]{busy_=true;await(engine_.configure(id_,limits,policy),[this,finish]{if(finish){busy_=true;await(engine_.startSelected(id_,!start_->isChecked()),[this]{error_->setText("Torrent added.");});}else error_->setText("Torrent settings saved.");});});}catch(const std::exception& e){busy_=false;error_->setText(QString::fromUtf8(e.what()));}}
    static QTableWidget* table(const QStringList& labels){auto* t=new QTableWidget(0,labels.size());t->setHorizontalHeaderLabels(labels);t->setEditTriggers(QAbstractItemView::NoEditTriggers);t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);t->verticalHeader()->hide();return t;}
public:
    DetailsDialog(TorrentEngine& engine,QString id,QWidget* parent):Dialog(parent),engine_(engine),id_(std::move(id)),rates_(settingsForm_,"torrentTask"),seeds_(settingsForm_){
        setObjectName("torrentDetailsDialog");setProperty("torrentId",id_);setWindowTitle("Torrent details");auto* layout=new QVBoxLayout(this);auto* tabs=new QTabWidget;tabs->setObjectName("torrentTabs");layout->addWidget(tabs,1);
        auto* summary=new QWidget;auto* summaryLayout=new QVBoxLayout(summary);overview_=plain("Loading torrent…");overview_->setObjectName("torrentOverview");summaryLayout->addWidget(overview_);summaryLayout->addStretch();tabs->addTab(summary,"Overview");files_=new FileTable;tabs->addTab(files_,"Files");trackers_=table({"Tracker host","Status","Failures","Last error"});trackers_->setObjectName("torrentTrackers");tabs->addTab(trackers_,"Trackers");peers_=table({"Address","Client","Download","Upload","Role"});peers_->setObjectName("torrentPeers");tabs->addTab(peers_,"Peers");auto* settings=new QWidget;settings->setLayout(settingsForm_);tabs->addTab(settings,"Settings");error(layout);
        start_=new QCheckBox("Start downloading after confirming files");start_->setObjectName("torrentPreviewStart");start_->hide();layout->addWidget(start_);auto* footer=new QHBoxLayout;apply_=makeButton("Apply files and settings","torrentApply");apply_->setEnabled(false);confirm_=makeButton("Confirm files","torrentPreviewConfirm");confirm_->hide();auto* close=makeButton("Close","torrentDetailsClose");footer->addWidget(apply_);footer->addWidget(confirm_);footer->addStretch();footer->addWidget(close);layout->addLayout(footer);connect(apply_,&QPushButton::clicked,this,[this]{save(false);});connect(confirm_,&QPushButton::clicked,this,[this]{save(true);});connect(close,&QPushButton::clicked,this,&QDialog::reject);auto* timer=new QTimer(this);timer->setInterval(750);connect(timer,&QTimer::timeout,this,[this]{poll();});timer->start();poll();
    }
};
class SettingsDialog final:public Dialog {
    TorrentEngine& engine_;QFormLayout* form_=new QFormLayout;RateForm rates_;QPushButton* save_;bool loaded_=false;
public:
    SettingsDialog(TorrentEngine& engine,QWidget* parent):Dialog(parent),engine_(engine),rates_(form_,"torrentGlobal"){
        setObjectName("torrentSettingsDialog");setWindowTitle("Torrent limits");setMinimumSize(480,300);resize(560,340);auto* layout=new QVBoxLayout(this);layout->addWidget(plain("These limits apply to all torrents together. HTTP downloads keep their existing settings."));layout->addLayout(form_);error(layout);layout->addStretch();auto* footer=new QHBoxLayout;auto* cancel=makeButton("Cancel","torrentSettingsCancel");save_=makeButton("Save","torrentSettingsSave");save_->setEnabled(false);save_->setProperty("primary",true);footer->addStretch();footer->addWidget(cancel);footer->addWidget(save_);layout->addLayout(footer);connect(cancel,&QPushButton::clicked,this,&QDialog::reject);connect(save_,&QPushButton::clicked,this,[this]{if(busy_)return;busy_=true;save_->setEnabled(false);await(engine_.setSessionLimits(rates_.get()),[this]{accept();});});await(engine_.sessionLimits(),[this](Limits limits){rates_.set(limits);loaded_=true;save_->setEnabled(true);});auto* retry=new QTimer(this);retry->setInterval(250);connect(retry,&QTimer::timeout,this,[this]{save_->setEnabled(loaded_&&!busy_);});retry->start();
    }
};
}
void showAddTorrent(TorrentEngine& engine,QString folder,QWidget* parent){if(auto* old=parent->findChild<QDialog*>("addTorrentDialog")){old->showNormal();old->raise();return;}auto* dialog=new AddDialog(engine,std::move(folder),parent);dialog->show();}
void showTorrentDetails(TorrentEngine& engine,QString id,QWidget* parent){for(auto* old:parent->findChildren<QDialog*>("torrentDetailsDialog"))if(old->property("torrentId").toString()==id){old->showNormal();old->raise();return;}auto* dialog=new DetailsDialog(engine,std::move(id),parent);dialog->show();}
void showTorrentSettings(TorrentEngine& engine,QWidget* parent){if(auto* old=parent->findChild<QDialog*>("torrentSettingsDialog")){old->showNormal();old->raise();return;}auto* dialog=new SettingsDialog(engine,parent);dialog->show();}
}
