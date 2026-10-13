#include "MainWindow.h"
#include "core/DownloadQueue/Categories.h"
#include <QApplication>
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
    auto* cap=new QSpinBox;cap->setObjectName("addSpeedLimit");cap->setRange(0,10485760);cap->setSuffix(" KB/s");cap->setSpecialValueText("Unlimited");form->addRow("Speed limit",cap);
    auto* start=new QCheckBox("Start immediately");start->setObjectName("addStart");start->setChecked(true);layout->addWidget(start);
    auto* note=new QLabel("Existing files are preserved. Some servers require a fresh start instead of resume.");note->setWordWrap(true);note->setObjectName("muted");layout->addWidget(note);
    auto* feedback=new QLabel;feedback->setObjectName("addError");feedback->setTextFormat(Qt::PlainText);feedback->setWordWrap(true);feedback->setStyleSheet("color:#ef4444;");layout->addWidget(feedback);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);auto* accept=buttons->addButton("Add download",QDialogButtonBox::AcceptRole);accept->setObjectName("addSubmit");accept->setProperty("primary",true);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(accept,&QPushButton::clicked,dialog,[this,dialog,url,dest,sha,priority,cap,start,accept,feedback]{accept->setEnabled(false);feedback->clear();const auto index=priority->currentIndex();
        auto future=queue_.add(url->text(),dest->text(),index==1 ? 1 : index==2 ? -1:0,start->isChecked(),sha->text(),static_cast<std::uint64_t>(cap->value())*1024);
        auto pending=std::make_shared<std::future<QString>>(std::move(future));auto* timer=new QTimer(dialog);timer->setInterval(30);connect(timer,&QTimer::timeout,dialog,[this,dialog,accept,timer,pending,feedback]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{pending->get();dialog->accept();banner_->hide();refresh();}catch(const std::exception& e){accept->setEnabled(true);feedback->setText(QString::fromUtf8(e.what()));message(QString::fromUtf8(e.what()),true);}});timer->start();
    });dialog->open();url->setFocus();
}
void MainWindow::confirmDownload(const QueueEntry& row) {
    if(findChild<QDialog*>("captureDialog-"+row.id))return;
    auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("captureDialog-"+row.id);dialog->setWindowTitle("Save download — Downshift");dialog->resize(560,220);
    auto* layout=new QVBoxLayout(dialog);auto* name=new QLabel(QFileInfo(row.destination).fileName());name->setTextFormat(Qt::PlainText);name->setWordWrap(true);layout->addWidget(name);auto* path=new QLineEdit(row.destination);path->setObjectName("captureDestination");layout->addWidget(path);
    auto* browse=new QPushButton("Choose location…");layout->addWidget(browse);connect(browse,&QPushButton::clicked,dialog,[dialog,path]{const auto file=QFileDialog::getSaveFileName(dialog,"Save download",path->text());if(!file.isEmpty())path->setText(file);});
    auto* error=new QLabel;error->setWordWrap(true);error->setTextFormat(Qt::PlainText);layout->addWidget(error);auto* actions=new QDialogButtonBox(QDialogButtonBox::Cancel);auto* start=actions->addButton("Start download",QDialogButtonBox::AcceptRole);start->setObjectName("captureStart");start->setProperty("primary",true);layout->addWidget(actions);connect(actions,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(start,&QPushButton::clicked,dialog,[this,dialog,path,start,error,row]{start->setEnabled(false);auto pending=std::make_shared<std::future<void>>(queue_.rename(row.id,path->text()));auto* timer=new QTimer(dialog);timer->setInterval(30);
        connect(timer,&QTimer::timeout,dialog,[this,dialog,start,error,pending,timer,row]{if(pending->wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;timer->stop();timer->deleteLater();try{pending->get();watch(queue_.action(row.id,QueueAction::Start),[guard=QPointer<QDialog>(dialog)]{if(guard)guard->accept();});}catch(const std::exception& e){start->setEnabled(true);error->setText(QString::fromUtf8(e.what()));}});timer->start();
    });dialog->show();raise();activateWindow();
}
void MainWindow::openSettings() {
    if(!ready_)return;auto* dialog=new QDialog(this);dialog->setObjectName("settingsDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Settings");dialog->setMinimumWidth(580);auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(24,24,24,24);layout->setSpacing(14);auto* form=new QFormLayout;layout->addLayout(form);auto saved=current_.settings;
    auto* directory=new QLineEdit(saved.directory);directory->setObjectName("settingsDirectory");auto* browse=makeButton("Browse…","settingsBrowse");auto* pathRow=new QHBoxLayout;pathRow->addWidget(directory,1);pathRow->addWidget(browse);form->addRow("Download folder",pathRow);connect(browse,&QPushButton::clicked,dialog,[dialog,directory]{const auto folder=QFileDialog::getExistingDirectory(dialog,"Download folder",directory->text());if(!folder.isEmpty())directory->setText(folder);});
    auto spin=[&](const char* name,int value,int maximum,const QString& title){auto* s=new QSpinBox;s->setObjectName(name);s->setRange(1,maximum);s->setValue(value);form->addRow(title,s);return s;};
    auto* simultaneous=spin("settingsSimultaneous",saved.simultaneous,16,"Simultaneous downloads");auto* host=spin("settingsPerHost",saved.perHost,16,"Downloads per site");auto* hostConnections=spin("settingsHostConnections",saved.hostConnections,64,"Connections per site");
    auto* connections=new QComboBox;connections->setObjectName("settingsConnections");connections->addItem("Automatic",0);for(int count:{1,4,8,16})connections->addItem(QString::number(count),count);const int choice=connections->findData(saved.automatic ? 0:saved.connections);connections->setCurrentIndex(choice>=0 ? choice:0);form->addRow("Connections per download",connections);
    auto* cap=new QSpinBox;cap->setObjectName("settingsSpeedLimit");cap->setRange(0,10485760);cap->setSuffix(" KB/s");cap->setSpecialValueText("Unlimited");cap->setValue(static_cast<int>(saved.speedLimit/1024));form->addRow("Total speed limit",cap);
    auto* themeChoice=new QComboBox;themeChoice->setObjectName("settingsTheme");themeChoice->addItems({"Light","Dark"});themeChoice->setCurrentIndex(saved.dark ? 1:0);form->addRow("Appearance",themeChoice);
    auto checkbox=[&](const char* name,const QString& text,bool value){auto* c=new QCheckBox(text);c->setObjectName(name);c->setChecked(value);layout->addWidget(c);return c;};
    auto* startup=checkbox("settingsStartup","Start queued downloads when Downshift opens",saved.startOnLaunch);auto* notifications=checkbox("settingsNotifications","Notify me when a download completes",saved.notifications);auto* diagnostics=checkbox("settingsDiagnostics","Save basic activity logs (no links or credentials)",saved.diagnostics);
    auto* chrome=new QLabel("Chrome integration: "+chromeStatus_);chrome->setObjectName("chromeStatus");layout->addWidget(chrome);auto* note=new QLabel("Connection changes apply to new workers. Total speed changes apply during active downloads.");note->setWordWrap(true);note->setObjectName("muted");layout->addWidget(note);
    auto* about=new QLabel("Downshift "+QApplication::applicationVersion()+" · Built with Qt under LGPLv3");about->setWordWrap(true);layout->addWidget(about);
    auto* licenses=makeButton("Licenses and library source","licensesButton");layout->addWidget(licenses);connect(licenses,&QPushButton::clicked,dialog,[]{QDesktopServices::openUrl(QUrl("https://github.com/ushalabs/Downshift-Download-Manager/blob/main/docs/THIRD_PARTY_NOTICES.md"));});
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setObjectName("settingsSave");buttons->button(QDialogButtonBox::Save)->setProperty("primary",true);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,saved,directory,simultaneous,host,hostConnections,connections,cap,themeChoice,startup,notifications,diagnostics]() mutable {
        saved.directory=directory->text();saved.simultaneous=simultaneous->value();saved.perHost=host->value();saved.hostConnections=hostConnections->value();const int count=connections->currentData().toInt();saved.automatic=count==0;saved.connections=count ? count:16;saved.speedLimit=static_cast<std::uint64_t>(cap->value())*1024;saved.dark=themeChoice->currentIndex()==1;saved.startOnLaunch=startup->isChecked();saved.notifications=notifications->isChecked();saved.diagnostics=diagnostics->isChecked();
        watch(queue_.configure(saved),[this,guard=QPointer<QDialog>(dialog)]{if(guard)guard->accept();message("Settings saved.");});
    });dialog->open();
}
void MainWindow::more() {
    const auto* e=selected();if(!e)return;const auto row=*e;QMenu menu(this);
    if(row.status==QueueStatus::Completed) {
        menu.addAction("Open file",[this,row]{if(QMessageBox::question(this,"Open downloaded file","Open this file with its Windows application?\n\n"+QFileInfo(row.destination).fileName())==QMessageBox::Yes)if(!QDesktopServices::openUrl(QUrl::fromLocalFile(row.destination)))message("Windows could not open this file.",true);});
        menu.addAction("Delete file and remove history…",[this,row]{if(QMessageBox::warning(this,"Delete downloaded file","Permanently delete this downloaded file and its history?\n\n"+row.destination,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)watch(queue_.deleteCompleted(row.id),[this]{message("File deleted.");});});
    }
    menu.addAction("Open folder",[this,row]{const auto folder=row.media && row.status!=QueueStatus::Completed ? row.mediaFolder:QFileInfo(row.destination).absolutePath();if(!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))message("Windows could not open this folder.",true);});menu.addAction("Copy source link",[row]{QApplication::clipboard()->setText(row.url);});menu.addSeparator();
    if(row.media) {
        if(row.status==QueueStatus::Running)menu.addAction("Show video window",[this,row]{openVideo(row.url,row.mediaTitle,row.mediaFolder);});
        else {if(row.status!=QueueStatus::Completed)menu.addAction("Retry video",[this,row]{openVideo(row.url,row.mediaTitle,row.mediaFolder);});menu.addAction("Remove from history",[this,row]{watch(queue_.action(row.id,QueueAction::Remove),[this]{message("History removed. Files were preserved.");});});}
        menu.exec(QCursor::pos());return;
    }
    if(row.status==QueueStatus::Prepared) {menu.addAction("Cancel pending browser handoff",[this,row]{watch(queue_.handoff(row.handoffKey,"abort"),[this](const QJsonObject&){message("Handoff cancelled. Turn capture off in the extension to resume in Chrome.");});});menu.exec(QCursor::pos());return;}
    auto* priority=menu.addMenu("Priority");for(const auto& [text,action]:std::vector<std::pair<QString,QueueAction>>{{"High",QueueAction::HighPriority},{"Normal",QueueAction::NormalPriority},{"Low",QueueAction::LowPriority}})priority->addAction(text,[this,row,action]{watch(queue_.action(row.id,action),[this]{banner_->hide();});});
    if(row.status!=QueueStatus::Completed && row.status!=QueueStatus::Running && row.status!=QueueStatus::Prepared) {
        menu.addAction("Restart from beginning…",[this,row]{if(QMessageBox::question(this,"Restart download","Archive the existing partial and download this file again?")==QMessageBox::Yes)watch(queue_.action(row.id,QueueAction::Restart),[this]{banner_->hide();});});
        menu.addAction("Change filename…",[this,row]{bool ok=false;const auto name=QInputDialog::getText(this,"Change filename","Filename",QLineEdit::Normal,QFileInfo(row.destination).fileName(),&ok);if(ok && !name.isEmpty()){if(name.contains('/') || name.contains('\\'))message("Enter a filename without folders.",true);else watch(queue_.rename(row.id,QDir(QFileInfo(row.destination).absolutePath()).filePath(name)),[this]{banner_->hide();});}});
    }
    if(row.status!=QueueStatus::Running && row.status!=QueueStatus::Prepared)menu.addAction("Remove from history",[this,row]{watch(queue_.action(row.id,QueueAction::Remove),[this]{message("History removed. Files were preserved.");});});
    menu.exec(QCursor::pos());
}
}
