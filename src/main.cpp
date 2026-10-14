#include "core/Runtime.h"
#include "gui/MainWindow.h"
#include "gui/MediaDialog.h"
#include "browser/PipeServer.h"
#include "browser/NativeProtocol.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>

#include <QApplication>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <exception>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Downshift"));
    QApplication::setOrganizationName(QStringLiteral("Usha Labs"));
    QApplication::setApplicationVersion(QStringLiteral(DOWNSHIFT_VERSION));
    const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));

    try {
        downshift::CurlRuntime curl;
        struct Instance {HANDLE mutex=nullptr;~Instance(){if(mutex)CloseHandle(mutex);}} instance;
        if(!smokeTest) {
            const auto name=("Local\\Downshift-"+downshift::browser::userSid()).toStdWString();instance.mutex=CreateMutexW(nullptr,FALSE,name.c_str());
            if(!instance.mutex)throw std::runtime_error("Downshift could not establish its local instance.");
            if(GetLastError()==ERROR_ALREADY_EXISTS) {
                std::jthread activate([&application]{try{(void)downshift::browser::send({{"version",1},{"command","open"}});}catch(...){}QMetaObject::invokeMethod(&application,&QCoreApplication::quit,Qt::QueuedConnection);});
                return application.exec();
            }
        }
        (void)downshift::checkDependencies();
        QTemporaryDir temporary;
        if(smokeTest && !temporary.isValid())throw std::runtime_error("The temporary startup-check folder is unavailable. Saved history was not opened.");
        QString directory=temporary.path();
        if(!smokeTest){
            PWSTR known=nullptr;
            if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&known)))throw std::runtime_error("Windows could not locate your saved download history.");
            directory=QDir(QString::fromWCharArray(known)).filePath("Usha Labs/Downshift");CoTaskMemFree(known);
            if(directory.isEmpty())throw std::runtime_error("The saved download history location is unavailable.");
        }
        downshift::QueueManager queue(QDir(directory).filePath("queue.sqlite"));
        downshift::MainWindow window(queue);
        if(smokeTest)window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        std::unique_ptr<downshift::browser::PipeServer> browser;
        if(!smokeTest) {
            browser=std::make_unique<downshift::browser::PipeServer>(queue,[&window]{QMetaObject::invokeMethod(&window,[&window]{window.showNormal();window.raise();window.activateWindow();},Qt::QueuedConnection);},[&window,&queue](QString url,QString title){
                QMetaObject::invokeMethod(&window,[&window,&queue,url=std::move(url),title=std::move(title)]{
                    window.openVideo(url,title);
                },Qt::QueuedConnection);
            },[&window]{QMetaObject::invokeMethod(&window,[&window]{window.captureError();},Qt::QueuedConnection);});
            auto* status=new QTimer(&window);status->setInterval(1000);QObject::connect(status,&QTimer::timeout,&window,[&window,&browser]{window.chromeStatus(browser->active() ? "Ready for the extension":"Unavailable");});status->start();
        }

        if (smokeTest) {
            QTimer::singleShot(1000, &window, [&application, &window] {
                const bool healthy = window.isVisible() && window.centralWidget() != nullptr &&
                    window.windowTitle() == QStringLiteral("Downshift") && window.ready() && !window.grab().isNull();
                application.exit(healthy ? 0 : 1);
            });
        }
        return application.exec();
    } catch (const std::exception& error) {
        if (!smokeTest) {
            QMessageBox::critical(nullptr, QStringLiteral("Downshift could not start"),
                QString::fromUtf8(error.what()));
        } else {
            qCritical("Downshift startup check failed: %s", error.what());
        }
        return 1;
    }
}
