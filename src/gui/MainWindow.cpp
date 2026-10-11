#include "gui/MainWindow.h"

#include <QApplication>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace downshift {

MainWindow::MainWindow(const DependencyInfo& dependencies, QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("downshiftMainWindow"));
    setWindowTitle(QStringLiteral("Downshift"));
    resize(800, 500);
    setMinimumSize(480, 300);

    auto* surface = new QWidget(this);
    auto* layout = new QVBoxLayout(surface);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    auto* title = new QLabel(QStringLiteral("Downshift"), surface);
    auto font = title->font();
    font.setPointSize(26);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);

    auto* description = new QLabel(
        QStringLiteral("A native Windows download manager.\n"
                       "The application foundation is under development."), surface);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* versions = new QLabel(
        QStringLiteral("Qt %1  |  libcurl %2  |  SQLite %3")
            .arg(QString::fromLatin1(qVersion()),
                 QString::fromStdString(dependencies.curlVersion),
                 QString::fromStdString(dependencies.sqliteVersion)), surface);
    versions->setWordWrap(true);
    layout->addStretch();
    layout->addWidget(versions);
    setCentralWidget(surface);
    statusBar()->showMessage(QStringLiteral("Downshift %1").arg(QApplication::applicationVersion()));
}

} // namespace downshift
