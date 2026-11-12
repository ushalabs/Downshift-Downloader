#include "MsixSetup.h"
#include "core/DownloadQueue/QueueManager.h"
#include "gui/Brand.h"
#include "gui/Design.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <appmodel.h>
#include <ShlObj.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QProcess>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <functional>

namespace downshift::msix {
namespace {
QDir appDirectory() {
    return QDir(QCoreApplication::applicationDirPath());
}

QString localAppData() {
    PWSTR path=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&path)))return {};
    const auto directory=QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return directory;
}

QString powershell() {
    std::array<wchar_t, MAX_PATH> directory{};
    const auto size=GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if(!size || size>=directory.size())return {};
    return QDir(QString::fromWCharArray(directory.data())).filePath("WindowsPowerShell/v1.0/powershell.exe");
}

bool installedSetup() {
    const auto app=appDirectory();
    return app.exists("msix-setup.json") || app.exists("downshift-setup.ini");
}

QByteArray readSetupFile(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size()>65536)return {};
    return file.readAll();
}

bool toolsReady(const QDir& destination) {
    if(destination.path().isEmpty() || !destination.isAbsolute())return false;
    const auto lock=readSetupFile(appDirectory().filePath("scripts/media-tools-lock.json"));
    const auto files=QJsonDocument::fromJson(lock).object().value("files").toArray();
    const auto receipt=QJsonDocument::fromJson(readSetupFile(destination.filePath("installed-files.json"))).object();
    if(files.isEmpty() || receipt.value("schema").toInt()!=1 ||
        QDir::cleanPath(QDir::fromNativeSeparators(receipt.value("root").toString())).compare(destination.path(),Qt::CaseInsensitive)!=0 ||
        receipt.value("lockSha256").toString().toLatin1()!=QCryptographicHash::hash(lock,QCryptographicHash::Sha256).toHex())return false;
    for(const auto& file:files){
        const QFileInfo entry(destination.filePath(file.toObject().value("name").toString()));
        if(!entry.isFile() || entry.size()<=0)return false;
    }
    return true;
}

void configureProcess(QProcess& process) {
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setWorkingDirectory(appDirectory().path());
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* values){values->flags|=CREATE_NO_WINDOW;});
}

void runScript(QProcess& process,const QString& script,const QStringList& arguments) {
    process.start(powershell(),QStringList{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",
        appDirectory().filePath("scripts/"+script)}+arguments);
}

class ChromeSetup final : public QGroupBox {
public:
    explicit ChromeSetup(QWidget* parent,bool allowDisconnect=false):QGroupBox("Browser extension",parent) {
        auto* layout=new QVBoxLayout(this);
        auto* summary=new QLabel("Set up the extension to send browser downloads to Downshift.");
        summary->setWordWrap(true);
        layout->addWidget(summary);
        browser_=new QComboBox;
        browser_->setObjectName("setupBrowser");
        browser_->addItems({"Chrome","Edge","Firefox","Opera"});
        layout->addWidget(browser_);
        const auto config=QJsonDocument::fromJson(readSetupFile(appDirectory().filePath("msix-setup.json"))).object();
        if(config.value("chromeWebStoreUrl").toString().isEmpty()){
            auto* instruction=new QLabel("1. Click Open Chrome Extensions below. Turn on Developer mode in Chrome.\n"
                "2. Click Load unpacked.\n"
                "3. Press Ctrl+V to paste the folder path, then press Enter. If prompted, click Select Folder.\n\n"
                "If Downshift is already installed in Chrome, click Reload on its extension card instead.");
            instruction->setObjectName("chromeSetupSteps");
            steps_=instruction;
            instruction->setWordWrap(true);
            layout->addWidget(instruction);
        }
        auto* buttons=new QHBoxLayout;
        connect_=new QPushButton(config.value("chromeWebStoreUrl").toString().isEmpty() ?
            "Open Chrome Extensions" : "Open Chrome Web Store");
        connect_->setObjectName("setupChromeButton");
        connect_->setAutoDefault(false);
        connect(browser_,&QComboBox::currentTextChanged,this,[this](const QString& browser){
            connect_->setText(browser=="Firefox" ? "Open Firefox Add-on Debugging":"Open "+browser+" Extensions");
            if(!steps_)return;
            if(browser=="Firefox")steps_->setText("1. Click Open Firefox Add-on Debugging.\n2. Click Load Temporary Add-on.\n3. Press Ctrl+V to paste the manifest path, then press Enter.\n\nFor permanent installation, use the Mozilla-signed add-on. Temporary add-ons are removed when Firefox closes.");
            else steps_->setText("1. Click Open "+browser+" Extensions below. Turn on Developer mode.\n2. Click Load unpacked.\n3. Press Ctrl+V to paste the folder path, then press Enter. If prompted, click Select Folder.");
        });
        buttons->addWidget(connect_);
        if(allowDisconnect && !familyName().isEmpty()){
            disconnect_=new QPushButton("Disconnect Chrome");
            buttons->addWidget(disconnect_);
            connect(disconnect_,&QPushButton::clicked,this,[this]{run(false);});
        }
        buttons->addStretch();
        layout->addLayout(buttons);
        status_=new QLabel;
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        layout->addWidget(status_);

        configureProcess(process_);
        connect(connect_,&QPushButton::clicked,this,[this]{run(true);});
        connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart)finish("Chrome setup could not start. Try again.");
        });
        connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus result){
            const auto output=QString::fromUtf8(process_.readAll()).trimmed();
            if(result!=QProcess::NormalExit || code!=0){
                finish(output.isEmpty() ? "Chrome setup did not finish. Try again." : output.left(500));
                return;
            }
            if(connecting_ && output.contains("DOWNSHIFT_UNPACKED_READY")){
                const auto state=stateDirectory();
                QString folder;
                if(browser_->currentText()=="Chrome")folder=state.isEmpty() ? appDirectory().filePath("extension"):QDir(state).filePath("ChromeExtension");
                else folder=state.isEmpty() ? appDirectory().filePath("browser-extensions/"+browser_->currentText().toLower()):QDir(state).filePath("BrowserExtensions/"+browser_->currentText().toLower());
                if(browser_->currentText()=="Firefox")folder=QDir(folder).filePath("manifest.json");
                QApplication::clipboard()->setText(QDir::toNativeSeparators(folder));
                finish(browser_->currentText()+" is open. The extension path is copied.");
            }else{
                finish(connecting_ ? "Chrome is open. Select Add to Chrome and approve the extension." :
                    "Downshift disconnected. You can remove the extension in Chrome.");
            }
        });
    }
    bool busy() const { return busy_; }
    std::function<void()> finished;

private:
    void run(bool connecting) {
        connecting_=connecting;
        busy_=true;
        connect_->setEnabled(false);
        browser_->setEnabled(false);
        if(disconnect_)disconnect_->setEnabled(false);
        status_->setText("Updating the Chrome connection…");
        const auto state=stateDirectory();
        if(familyName().isEmpty()){
            runScript(process_,"Open-ChromeSetup.ps1",{"-OpenChrome","-Browser",browser_->currentText()});
        }else if(state.isEmpty()){
            finish("Windows could not locate Downshift’s Chrome setup folder.");
        }else{
            runScript(process_,"Setup-MsixChrome.ps1",{"-StateDirectory",state,"-Browser",browser_->currentText(),connecting ? "-OpenChrome":"-Disconnect"});
        }
    }
    void finish(const QString& message) {
        status_->setText(message);
        busy_=false;
        connect_->setEnabled(true);
        browser_->setEnabled(true);
        if(disconnect_)disconnect_->setEnabled(true);
        if(finished)finished();
    }
    QProcess process_;
    QPushButton* connect_;
    QComboBox* browser_;
    QLabel* steps_=nullptr;
    QPushButton* disconnect_=nullptr;
    QLabel* status_;
    bool busy_=false;
    bool connecting_=true;
};

class VideoSetupDialog final : public QDialog {
public:
    explicit VideoSetupDialog(const QDir& destination):destination_(destination) {
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint);
        setWindowTitle("Set up Downshift");
        setWindowIcon(brandIcon());
        setObjectName("firstLaunchSetup");
        setMinimumWidth(470);
        auto* layout=new QVBoxLayout(this);
        layout->setContentsMargins(24,20,24,20);
        layout->setSpacing(13);
        auto* title=new QLabel("Installing video tools");
        title->setStyleSheet("font-size: 21px; font-weight: 600;");
        layout->addWidget(title);
        auto* summary=new QLabel("Downshift will open when its required video tools are downloaded and verified.");
        summary->setWordWrap(true);
        layout->addWidget(summary);
        progress_=new QProgressBar;
        progress_->setTextVisible(false);
        const auto colors=DesignTokens::theme(QueueSettings{}.dark);
        progress_->setStyleSheet(QString("QProgressBar { background: %1; border: none; border-radius: 5px; min-height: 10px; } "
            "QProgressBar::chunk { background: %2; border-radius: 5px; }").arg(colors.input.name(),colors.pink.name()));
        layout->addWidget(progress_);
        size_=new QLabel;
        size_->setObjectName("videoSetupDownloadSize");
        size_->setWordWrap(true);
        layout->addWidget(size_);
        status_=new QLabel;
        status_->setObjectName("videoSetupStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        if(chromeSetupAvailable()){
            chrome_=new ChromeSetup(this);
            layout->addWidget(chrome_);
            chrome_->finished=[this]{continueIfReady();};
        }
        auto* buttons=new QHBoxLayout;
        buttons->addStretch();
        exit_=new QPushButton("Exit Downshift");
        retry_=new QPushButton("Retry");
        retry_->setObjectName("retryVideoSetup");
        buttons->addWidget(exit_);
        buttons->addWidget(retry_);
        layout->addLayout(buttons);
        exit_->hide();
        retry_->hide();
        connect(exit_,&QPushButton::clicked,this,[this]{QDialog::reject();});
        connect(retry_,&QPushButton::clicked,this,[this]{install();});

        configureProcess(process_);
        connect(&process_,&QProcess::readyRead,this,[this]{
            output_.append(process_.readAll());
            while(const auto lineEnd=output_.indexOf('\n')+1){
                const auto line=QString::fromUtf8(output_.left(lineEnd)).trimmed();
                output_.remove(0,lineEnd);
                if(line.startsWith("Downshift setup: "))status_->setText(line.mid(17));
                if(line.startsWith("Downshift progress: ")){
                    const auto values=line.mid(20).split(' ');
                    if(values.size()==2)showDownloadProgress(values[0].toLongLong(),values[1].toLongLong());
                }
            }
        });
        connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart)fail("Windows could not start the video tool installer.");
        });
        connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus result){
            if(result!=QProcess::NormalExit || code!=0 || !toolsReady(destination_)){
                fail("Video tools could not be installed or verified. Check your internet connection and available disk space, then retry.");
                return;
            }
            ready_=true;
            progress_->setRange(0,1);
            progress_->setValue(1);
            status_->setText("Video tools installed and verified.");
            continueIfReady();
        });
        QTimer::singleShot(0,this,[this]{install();});
    }

    void reject() override {
        // Escape and the window close command must never bypass setup.
    }

private:
    void showDownloadProgress(qint64 downloaded,qint64 total) {
        if(total<=0 || downloaded<0 || downloaded>total)return;
        const auto percent=static_cast<int>(100.0*downloaded/total);
        const QLocale locale;
        const auto format=[&locale](qint64 bytes){return locale.formattedDataSize(bytes,2,QLocale::DataSizeSIFormat);};
        progress_->setRange(0,100);
        progress_->setValue(percent);
        size_->setText(QString("%1 of %2 downloaded (%3%)\n%4 remaining")
            .arg(format(downloaded),format(total),QString::number(percent),format(total-downloaded)));
        size_->setToolTip(QString("%1 of %2 bytes downloaded").arg(locale.toString(downloaded),locale.toString(total)));
    }
    void install() {
        exit_->hide();
        retry_->hide();
        output_.clear();
        progress_->setRange(0,0);
        size_->setText("Getting download size…");
        status_->setText("Preparing video tools…");
        if(!destination_.isAbsolute()){
            fail("Windows could not locate a writable folder for video tools.");
            return;
        }
        runScript(process_,"Install-MediaTools.ps1",{"-Destination",destination_.path(),"-CacheDirectory",
            destination_.absoluteFilePath("../media-cache"),"-RemoveDownloadCache"});
    }
    void fail(const QString& message) {
        if(progress_->maximum()==0)progress_->setRange(0,100);
        status_->setText(message);
        exit_->show();
        retry_->show();
    }
    void continueIfReady() {
        if(ready_ && (!chrome_ || !chrome_->busy()))accept();
    }
    QDir destination_;
    QProcess process_;
    QByteArray output_;
    QProgressBar* progress_;
    QLabel* size_;
    QLabel* status_;
    QPushButton* exit_;
    QPushButton* retry_;
    ChromeSetup* chrome_=nullptr;
    bool ready_=false;
};
}

QString familyName() {
    std::array<wchar_t, PACKAGE_FAMILY_NAME_MAX_LENGTH+1> name{};
    UINT32 length=static_cast<UINT32>(name.size());
    if(GetCurrentPackageFamilyName(&length,name.data())!=ERROR_SUCCESS)return {};
    return QString::fromWCharArray(name.data());
}

QString stateDirectory() {
    const auto family=familyName();
    if(family.isEmpty())return {};
    const auto local=localAppData();
    if(local.isEmpty())return {};
    return QDir(local).filePath("Packages/"+family+"/LocalState");
}

QString mediaToolsDirectory() {
    if(!installedSetup())return appDirectory().filePath("media-tools");
    if(!familyName().isEmpty()){
        const auto state=stateDirectory();
        return state.isEmpty() ? QString() : QDir(state).filePath("media-tools");
    }
    const QSettings installer(appDirectory().filePath("downshift-setup.ini"),QSettings::IniFormat);
    if(installer.value("Setup/IsolatedTest").toString()=="1")return appDirectory().filePath("test-video-support/media-tools");
    const auto local=localAppData();
    return local.isEmpty() ? QString() : QDir(local).filePath("Downshift/VideoSupport/media-tools");
}

bool chromeSetupAvailable() {
    return installedSetup();
}

bool finishFirstLaunchSetup() {
    static QPointer<QDialog> activeSetup;
    if(activeSetup){
        activeSetup->showNormal();
        activeSetup->raise();
        activeSetup->activateWindow();
        return false;
    }
    if(!installedSetup())return true;
    const QDir destination(mediaToolsDirectory());
    if(toolsReady(destination))return true;
    applyDesign(QueueSettings{}.dark);
    VideoSetupDialog dialog(destination);
    activeSetup=&dialog;
    return dialog.exec()==QDialog::Accepted;
}

void showSetup(QWidget* parent) {
    if(!chromeSetupAvailable())return;
    auto* dialog=new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Connect browser — Downshift");
    auto* layout=new QVBoxLayout(dialog);
    layout->addWidget(new ChromeSetup(dialog,true));
    auto* close=new QPushButton("Done");
    layout->addWidget(close);
    QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::close);
    dialog->resize(450,dialog->sizeHint().height());
    dialog->show();
}
}
