#include "MainWindow.h"
#include "core/DownloadQueue/Categories.h"
#include "core/DownloadQueue/VideoUrl.h"
#include <QApplication>
#include "Controls.h"
#include "Design.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QDoubleValidator>
#include <set>
#include <cmath>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
namespace downshift {
void MainWindow::openAdd(QString address,QString filename) {
    if(!ready_) {message("Your queue is still loading. Try again in a moment.");return;}
    auto* dialog=new QDialog(this);dialog->setObjectName("addDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Add download");dialog->setMinimumWidth(560);auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(24,24,24,24);layout->setSpacing(16);auto* form=new QFormLayout;layout->addLayout(form);
    auto* url=new QLineEdit;url->setObjectName("addUrl");url->setPlaceholderText("https://example.com/file.zip");form->addRow("Download link",url);
    auto* dest=new QLineEdit;dest->setObjectName("addDestination");auto* browse=makeButton("Browse…","addBrowse");auto* pathRow=new QHBoxLayout;pathRow->addWidget(dest,1);pathRow->addWidget(browse);form->addRow("Save as",pathRow);
    connect(url,&QLineEdit::textChanged,dialog,[this,url,dest]{if(!dest->isModified()){auto name=QUrl(url->text()).fileName();if(name.isEmpty())name="download.bin";dest->setText(QDir(current_.settings.directory).filePath(downloadCategory(name)+"/"+name));}});
    dest->setText(QDir(current_.settings.directory).filePath("Programs/download.bin"));url->setText(address);if(!filename.isEmpty())dest->setText(QDir(current_.settings.directory).filePath(downloadCategory(filename)+"/"+filename));connect(browse,&QPushButton::clicked,dialog,[dialog,dest]{const auto name=QFileDialog::getSaveFileName(dialog,"Save download",dest->text());if(!name.isEmpty()){dest->setText(name);dest->setModified(true);}});
    auto* sha=new QLineEdit;sha->setObjectName("addSha256");sha->setPlaceholderText("Optional trusted SHA-256 checksum");form->addRow("Checksum",sha);
    auto* priority=new QComboBox;priority->setObjectName("addPriority");priority->addItems({"Normal","High","Low"});form->addRow("Priority",priority);
    auto* cap=new QSpinBox;cap->setObjectName("addSpeedLimit");cap->setButtonSymbols(QAbstractSpinBox::NoButtons);cap->setRange(0,10485760);cap->setSuffix(" KB/s");cap->setSpecialValueText("Unlimited");form->addRow("Speed limit",cap);
    auto* start=new QCheckBox("Start immediately");start->setObjectName("addStart");start->setChecked(true);layout->addWidget(start);
    auto* note=new QLabel("Existing files are preserved. Some servers require a fresh start instead of resume.");note->setWordWrap(true);note->setObjectName("muted");layout->addWidget(note);
    auto* feedback=new QLabel;feedback->setObjectName("addError");feedback->setTextFormat(Qt::PlainText);feedback->setWordWrap(true);feedback->setStyleSheet("color:#ef4444;");layout->addWidget(feedback);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);auto* accept=buttons->addButton("Add download",QDialogButtonBox::AcceptRole);accept->setObjectName("addSubmit");accept->setProperty("primary",true);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(accept,&QPushButton::clicked,dialog,[this,dialog,url,dest,sha,priority,cap,start,accept,feedback]{
        if(const auto video=youtubeVideo(url->text().trimmed());!video.isEmpty()){dialog->accept();openVideo(video,"YouTube video");return;}
        accept->setEnabled(false);feedback->clear();const auto index=priority->currentIndex();
        auto future=queue_.add(url->text(),dest->text(),index==1 ? 1 : index==2 ? -1:0,start->isChecked(),sha->text(),static_cast<std::uint64_t>(cap->value())*1024);
        auto pending=std::make_shared<std::future<QString>>(std::move(future));auto* timer=new QTimer(dialog);timer->setInterval(30);connect(timer,&QTimer::timeout,dialog,[this,dialog,accept,timer,pending,feedback]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{pending->get();dialog->accept();banner_->hide();refresh();}catch(const std::exception& e){accept->setEnabled(true);feedback->setText(QString::fromUtf8(e.what()));message(QString::fromUtf8(e.what()),true);}});timer->start();
    });for(auto* combo:dialog->findChildren<QComboBox*>())modernCombo(combo);dialog->open();url->setFocus();
}
void MainWindow::confirmDownload(const QueueEntry& row) {
    if(findChild<QDialog*>("captureDialog-"+row.id))return;
    auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("captureDialog-"+row.id);dialog->setWindowTitle("Save download — Downshift");dialog->resize(560,220);
    auto* layout=new QVBoxLayout(dialog);auto* name=new QLabel(QFileInfo(row.destination).fileName());name->setTextFormat(Qt::PlainText);name->setWordWrap(true);layout->addWidget(name);auto* path=new QLineEdit(row.destination);path->setObjectName("captureDestination");layout->addWidget(path);
    auto* browse=new QPushButton("Choose location…");layout->addWidget(browse);connect(browse,&QPushButton::clicked,dialog,[dialog,path]{const auto file=QFileDialog::getSaveFileName(dialog,"Save download",path->text());if(!file.isEmpty())path->setText(file);});
    auto* error=new QLabel;error->setWordWrap(true);error->setTextFormat(Qt::PlainText);layout->addWidget(error);auto* actions=new QDialogButtonBox(QDialogButtonBox::Cancel);auto* start=actions->addButton("Start download",QDialogButtonBox::AcceptRole);start->setObjectName("captureStart");start->setProperty("primary",true);layout->addWidget(actions);connect(actions,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(start,&QPushButton::clicked,dialog,[this,dialog,path,start,error,row]{start->setEnabled(false);auto pending=std::make_shared<std::future<void>>(queue_.rename(row.id,path->text()));auto* timer=new QTimer(dialog);timer->setInterval(30);
        connect(timer,&QTimer::timeout,dialog,[this,dialog,start,error,pending,timer,row]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{pending->get();watch(queue_.action(row.id,QueueAction::Start),[guard=QPointer<QDialog>(dialog)]{if(guard)guard->accept();});}catch(const std::exception& e){start->setEnabled(true);error->setText(QString::fromUtf8(e.what()));}});timer->start();
    });if(row.browserTransfer)connect(dialog,&QDialog::rejected,this,[this,row]{watch(queue_.action(row.id,QueueAction::Cancel),[]{});});dialog->show();raise();activateWindow();
}
void MainWindow::openSettings() {
    if(auto* old=findChild<QDialog*>("settingsDialog")){old->raise();old->activateWindow();return;}
    auto* dialog=new QDialog(this);dialog->setObjectName("settingsDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Settings — Downshift");dialog->resize(780,560);dialog->setMinimumSize(630,460);
    auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(20,20,20,16);layout->setSpacing(14);auto* heading=new QLabel("Settings");heading->setObjectName("heading");layout->addWidget(heading);
    auto* body=new QHBoxLayout;body->setSpacing(20);layout->addLayout(body,1);auto* sections=new QListWidget;sections->setObjectName("settingsSections");sections->addItems({"General","Downloads","Appearance","Integration","Advanced"});sections->setMaximumWidth(150);sections->setMinimumWidth(120);body->addWidget(sections);auto* pages=new QStackedWidget;pages->setObjectName("settingsPages");body->addWidget(pages,1);
    const auto page=[&](const QString& title,const QString& subtitle){auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto* surface=new QWidget;auto* box=new QVBoxLayout(surface);box->setContentsMargins(0,4,8,12);box->setSpacing(14);auto* label=new QLabel(title);label->setObjectName("sectionHeading");box->addWidget(label);auto* hint=new QLabel(subtitle);hint->setObjectName("muted");hint->setWordWrap(true);box->addWidget(hint);scroll->setWidget(surface);pages->addWidget(scroll);return box;};
    const auto form=[&](QVBoxLayout* box){auto* rows=new QFormLayout;rows->setSpacing(12);rows->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);rows->setRowWrapPolicy(QFormLayout::WrapLongRows);box->addLayout(rows);return rows;};
    const auto checkbox=[&](QVBoxLayout* box,const char* name,const QString& text,bool value){auto* field=new QCheckBox(text);field->setObjectName(name);field->setChecked(value);box->addWidget(field);return field;};
    const auto bounded=[&](QFormLayout* rows,const char* name,const QString& title,int value,std::initializer_list<int> offered){auto* field=new QComboBox;field->setObjectName(name);std::set<int> choices(offered);choices.insert(value);for(int choice:choices)field->addItem(QString::number(choice),choice);field->setCurrentIndex(field->findData(value));rows->addRow(title,field);modernCombo(field);return field;};
    auto saved=current_.settings;
    auto* general=page("General","Choose where downloads go and how Downshift opens.");auto* generalForm=form(general);auto* directory=new QLineEdit(saved.directory);directory->setObjectName("settingsDirectory");directory->setToolTip(saved.directory);auto* browse=makeButton("Choose…","settingsBrowse");auto* pathRow=new QHBoxLayout;pathRow->addWidget(directory,1);pathRow->addWidget(browse);generalForm->addRow("Download folder",pathRow);connect(browse,&QPushButton::clicked,dialog,[dialog,directory]{const auto folder=QFileDialog::getExistingDirectory(dialog,"Download folder",directory->text());if(!folder.isEmpty())directory->setText(folder);});
    auto* startup=checkbox(general,"settingsStartup","Start queued downloads when Downshift opens",saved.startOnLaunch);auto* notifications=checkbox(general,"settingsNotifications","Notify me when a download completes",saved.notifications);general->addStretch();
    auto* downloads=page("Downloads","Connection changes affect new workers. Speed limits also affect active downloads.");auto* downloadForm=form(downloads);
    auto* simultaneous=bounded(downloadForm,"settingsSimultaneous","Simultaneous downloads",saved.simultaneous,{1,2,3,4,5,8,10,16});auto* host=bounded(downloadForm,"settingsPerHost","Downloads per site",saved.perHost,{1,2,3,4,5});auto* hostConnections=bounded(downloadForm,"settingsHostConnections","Connections per site",saved.hostConnections,{4,8,16,24,32,48,64});
    auto* connections=new QComboBox;connections->setObjectName("settingsConnections");connections->addItem("Automatic",0);std::set<int> choices{1,4,8,16};choices.insert(saved.connections);for(int count:choices)connections->addItem(QString::number(count),count);connections->setCurrentIndex(connections->findData(saved.automatic ? 0:saved.connections));modernCombo(connections);downloadForm->addRow("Connections per download",connections);
    auto* cap=new QLineEdit(QString::number(static_cast<double>(saved.speedLimit)/1024.,'g',12));cap->setObjectName("settingsSpeedLimit");cap->setValidator(new QDoubleValidator(0,10485760,10,cap));cap->setPlaceholderText("0 = Unlimited");downloadForm->addRow("Total speed limit (KB/s)",cap);auto* capNote=new QLabel("0 means Unlimited. Fractions are supported; 1024 KB/s is 1 MB/s.");capNote->setWordWrap(true);capNote->setObjectName("muted");downloads->addWidget(capNote);downloads->addStretch();
    auto* appearance=page("Appearance","Neutral surfaces, clear text and Downshift's pink accents.");auto* appearanceForm=form(appearance);auto* themeChoice=new QComboBox;themeChoice->setObjectName("settingsTheme");themeChoice->addItems({"Light","Dark"});themeChoice->setCurrentIndex(saved.dark ? 1:0);modernCombo(themeChoice);appearanceForm->addRow("Theme",themeChoice);appearance->addStretch();connect(themeChoice,&QComboBox::currentIndexChanged,dialog,[this](int choice){theme(choice==1);});connect(dialog,&QDialog::rejected,this,[this]{theme(current_.settings.dark);});
    auto* integration=page("Integration","Use the Chrome extension to send downloads to this desktop app.");auto* chrome=new QLabel("Chrome integration: "+chromeStatus_);chrome->setObjectName("chromeStatus");chrome->setWordWrap(true);integration->addWidget(chrome);auto* native=new QLabel(ready_ ? "Saved history: loaded. Native Messaging is available when the extension reports Ready.":"Saved history is unavailable. Existing data has not been cleared; close Downshift and retry.");native->setWordWrap(true);integration->addWidget(native);auto* instruction=new QLabel("After an app update, reload Downshift at chrome://extensions and refresh open pages. Check that both app and extension versions match.");instruction->setWordWrap(true);instruction->setObjectName("muted");integration->addWidget(instruction);
    auto* diagnosticsCopy=makeButton("Copy integration diagnostics","integrationDiagnostics");diagnosticsCopy->setIcon(lineIcon(Symbol::Copy));integration->addWidget(diagnosticsCopy);connect(diagnosticsCopy,&QPushButton::clicked,dialog,[this]{QApplication::clipboard()->setText(QString("Downshift %1\nHistory loaded: %2\nDownloads: %3\nChrome: %4\nData folder: %5").arg(QApplication::applicationVersion(),ready_ ? "Yes":"No",QString::number(current_.entries.size()),chromeStatus_,QFileInfo(queue_.databasePath()).absolutePath()));});integration->addStretch();
    auto* advanced=page("Advanced","History and settings stay on this computer. Backups retain encrypted data.");auto* diagnostics=checkbox(advanced,"settingsDiagnostics","Save basic activity logs (no links or credentials)",saved.diagnostics);auto* dataLocation=new QLabel("Data folder: "+QFileInfo(queue_.databasePath()).absolutePath());dataLocation->setObjectName("dataFolder");dataLocation->setWordWrap(true);dataLocation->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);advanced->addWidget(dataLocation);auto* dataFolder=makeButton("Open data folder","dataFolderButton");dataFolder->setIcon(lineIcon(Symbol::Folder));advanced->addWidget(dataFolder);connect(dataFolder,&QPushButton::clicked,dialog,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(queue_.databasePath()).absolutePath()));});auto* backupNote=new QLabel("Up to eight encrypted snapshots are kept after successful startup, Pause and clean shutdown. Download files and resume journals remain in their chosen folders.");backupNote->setWordWrap(true);backupNote->setObjectName("muted");advanced->addWidget(backupNote);
    auto* licenses=makeButton("Licenses and library source","licensesButton");advanced->addWidget(licenses);connect(licenses,&QPushButton::clicked,dialog,[]{QDesktopServices::openUrl(QUrl("https://github.com/ushalabs/Downshift-Download-Manager/blob/main/docs/THIRD_PARTY_NOTICES.md"));});auto* version=new QLabel("Downshift "+QApplication::applicationVersion()+" · Qt / LGPLv3");version->setObjectName("muted");advanced->addWidget(version);advanced->addStretch();
    connect(sections,&QListWidget::currentRowChanged,pages,&QStackedWidget::setCurrentIndex);sections->setCurrentRow(ready_ ? 0:3);if(!ready_){for(int index:{0,1,2})pages->widget(index)->setEnabled(false);diagnostics->setEnabled(false);}
    auto* feedback=new QLabel;feedback->setObjectName("settingsError");feedback->setWordWrap(true);feedback->setTextFormat(Qt::PlainText);feedback->setStyleSheet("color:#dc4554;");layout->addWidget(feedback);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);auto* save=buttons->button(QDialogButtonBox::Save);save->setObjectName("settingsSave");save->setProperty("primary",true);save->setEnabled(ready_);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,saved,directory,simultaneous,host,hostConnections,connections,cap,themeChoice,startup,notifications,diagnostics,save,feedback]() mutable {
        bool valid=false;const auto speed=cap->text().toDouble(&valid);if(!valid || !std::isfinite(speed) || speed<0 || speed>10485760){feedback->setText("Enter a speed limit from 0 to 10485760 KB/s.");return;}
        const auto unchanged=QString::number(static_cast<double>(saved.speedLimit)/1024.,'g',12);saved.speedLimit=cap->text()==unchanged ? saved.speedLimit:static_cast<std::uint64_t>(std::llround(speed*1024.));saved.directory=directory->text();saved.simultaneous=simultaneous->currentData().toInt();saved.perHost=host->currentData().toInt();saved.hostConnections=hostConnections->currentData().toInt();const auto count=connections->currentData().toInt();saved.automatic=count==0;if(count)saved.connections=count;saved.dark=themeChoice->currentIndex()==1;saved.startOnLaunch=startup->isChecked();saved.notifications=notifications->isChecked();saved.diagnostics=diagnostics->isChecked();save->setEnabled(false);feedback->clear();
        auto pending=std::make_shared<std::future<void>>(queue_.configure(saved));auto* timer=new QTimer(dialog);timer->setInterval(30);connect(timer,&QTimer::timeout,dialog,[this,dialog,save,feedback,pending,timer]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{pending->get();dialog->accept();refresh();message("Settings saved.");}catch(const std::exception& error){save->setEnabled(true);feedback->setText(QString::fromUtf8(error.what()));}});timer->start();
    });dialog->open();
}

void MainWindow::more() {
    if(selectedTorrent()){torrentMenu();return;}
    const auto* e=selected();if(!e)return;const auto row=*e;QMenu menu(this);
    const auto removeHistory=[this,row]{if(QMessageBox::question(this,"Remove download history","Remove this history entry? The file and any partial download will be kept.",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)watch(queue_.action(row.id,QueueAction::Remove),[this]{message("History removed. Files were preserved.");});};
    for(const auto& pair:std::vector<std::pair<QPushButton*,QueueAction>>{{start_,QueueAction::Start},{pause_,QueueAction::Pause},{cancel_,QueueAction::Cancel}}){auto* action=menu.addAction(pair.first->icon(),pair.first->text(),[this,kind=pair.second]{perform(kind);});action->setEnabled(pair.first->isEnabled());}menu.addSeparator();
    if(row.status==QueueStatus::Completed) {
        menu.addAction("Open file",[this,row]{if(QMessageBox::question(this,"Open downloaded file","Open this file with its Windows application?\n\n"+QFileInfo(row.destination).fileName())==QMessageBox::Yes)if(!QDesktopServices::openUrl(QUrl::fromLocalFile(row.destination)))message("Windows could not open this file.",true);});
        menu.addAction("Delete file and remove history…",[this,row]{if(QMessageBox::warning(this,"Delete downloaded file","Permanently delete this downloaded file and its history?\n\n"+row.destination,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)watch(queue_.deleteCompleted(row.id),[this]{message("File deleted.");});});
    }
    menu.addAction("Open folder",[this,row]{const auto folder=row.media && row.status!=QueueStatus::Completed ? row.mediaFolder:QFileInfo(row.destination).absolutePath();if(!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))message("Windows could not open this folder.",true);});menu.addAction("Copy source link",[row]{QApplication::clipboard()->setText(row.url);});menu.addAction("Copy destination",[row]{QApplication::clipboard()->setText(row.destination);});menu.addSeparator();
    if(row.media) {
        if(row.status==QueueStatus::Running)menu.addAction("Show video window",[this,row]{openVideo(row.url,row.mediaTitle,row.mediaFolder);});
        else {if(row.status!=QueueStatus::Completed)menu.addAction("Retry video",[this,row]{openVideo(row.url,row.mediaTitle,row.mediaFolder);});menu.addAction("Remove from history…",removeHistory);}
        menu.exec(QCursor::pos());return;
    }
    if(row.status==QueueStatus::Prepared) {menu.addAction("Cancel pending browser handoff",[this,row]{watch(queue_.handoff(row.handoffKey,"abort"),[this](const QJsonObject&){message("Handoff cancelled. Turn capture off in the extension to resume in Chrome.");});});menu.exec(QCursor::pos());return;}
    auto* priority=menu.addMenu("Priority");for(const auto& [text,action]:std::vector<std::pair<QString,QueueAction>>{{"High",QueueAction::HighPriority},{"Normal",QueueAction::NormalPriority},{"Low",QueueAction::LowPriority}})priority->addAction(text,[this,row,action]{watch(queue_.action(row.id,action),[this]{banner_->hide();});});
    if(row.status!=QueueStatus::Completed && row.status!=QueueStatus::Running && row.status!=QueueStatus::Prepared) {
        menu.addAction("Restart from beginning…",[this,row]{if(QMessageBox::question(this,"Restart download","Archive the existing partial and download this file again?")==QMessageBox::Yes)watch(queue_.action(row.id,QueueAction::Restart),[this]{banner_->hide();});});
        menu.addAction("Change filename…",[this,row]{bool ok=false;const auto name=QInputDialog::getText(this,"Change filename","Filename",QLineEdit::Normal,QFileInfo(row.destination).fileName(),&ok);if(ok && !name.isEmpty()){if(name.contains('/') || name.contains('\\'))message("Enter a filename without folders.",true);else watch(queue_.rename(row.id,QDir(QFileInfo(row.destination).absolutePath()).filePath(name)),[this]{banner_->hide();});}});
    }
    if(row.status!=QueueStatus::Running && row.status!=QueueStatus::Prepared)menu.addAction("Remove from history…",removeHistory);
    menu.exec(QCursor::pos());
}
}
