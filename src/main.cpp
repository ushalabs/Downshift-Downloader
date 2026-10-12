#include "core/Runtime.h"
#include "gui/MainWindow.h"

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
        (void)downshift::checkDependencies();
        QTemporaryDir temporary;
        const auto directory=smokeTest ? temporary.path() : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        downshift::QueueManager queue(QDir(directory).filePath("queue.sqlite"));
        downshift::MainWindow window(queue);
        window.show();

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
