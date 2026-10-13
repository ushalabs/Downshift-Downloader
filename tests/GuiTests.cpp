#include "gui/MainWindow.h"
#include "core/Runtime.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message);++checks; }
template<class T> T* widget(QObject& parent,const char* name) { auto* result=parent.findChild<T*>(name); if(!result)throw std::runtime_error("Missing GUI control");return result; }
template<class F> void wait(F condition,int timeout=15000) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(std::chrono::steady_clock::now()<end) {QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(condition())return;QThread::msleep(5);}
    throw std::runtime_error("GUI condition timed out");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv); QApplication::setApplicationName("Downshift");QApplication::setOrganizationName("Usha Labs");
    if(argc!=5)return 2;const auto root=QString::fromLocal8Bit(argv[1]),url=QString::fromLatin1(argv[2]),sha=QString::fromLatin1(argv[3]),shots=QString::fromLocal8Bit(argv[4]);
    QDir().mkpath(shots);downshift::CurlRuntime curl;const auto database=QDir(root).filePath("queue.sqlite"),file=QDir(root).filePath("Verified download.bin");
    int beats=0;double maximumGap=0;bool measuring=false;auto last=std::chrono::steady_clock::now();QTimer heartbeat;heartbeat.setInterval(20);QObject::connect(&heartbeat,&QTimer::timeout,[&]{const auto now=std::chrono::steady_clock::now();if(measuring){maximumGap=std::max(maximumGap,std::chrono::duration<double>(now-last).count());++beats;}last=now;});heartbeat.start();
    try {
        {
            downshift::QueueManager queue(database);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready();});check(!window.current().settings.dark,"Light must be default");
            check(window.width()<950 && window.height()<650,"Compact default window");check(window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6),"Compact columns adapt");window.resize(1200,750);QApplication::processEvents();
            // A small Windows/CI display can clamp the requested native window size.
            std::cout<<"Responsive initial native size: "<<window.width()<<'x'<<window.height()<<"; connection column hidden="<<window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)<<'\n';
            wait([&]{return window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)==(window.width()<1100);},3000);
            std::cout<<"Responsive native size: "<<window.width()<<'x'<<window.height()<<"; connection column hidden="<<window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)<<'\n';
            check(window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6)==(window.width()<1100),"Columns follow actual native window width");
            QResizeEvent expanded(QSize(1200,750),window.size());QApplication::sendEvent(&window,&expanded);check(!window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6),"Expanded resize policy shows connection details");
            QResizeEvent compact(QSize(860,570),QSize(1200,750));QApplication::sendEvent(&window,&compact);check(window.findChild<QTableWidget*>("downloadTable")->isColumnHidden(6),"Compact resize policy hides connection details");window.resize(860,570);
            last=std::chrono::steady_clock::now();measuring=true;
            widget<QPushButton>(window,"addButton")->click();auto* add=widget<QDialog>(window,"addDialog");
            widget<QLineEdit>(*add,"addUrl")->setText(url);widget<QLineEdit>(*add,"addDestination")->setText(file);widget<QLineEdit>(*add,"addSha256")->setText(sha);widget<QSpinBox>(*add,"addSpeedLimit")->setValue(256);widget<QPushButton>(*add,"addSubmit")->click();
            auto* table=widget<QTableWidget>(window,"downloadTable");wait([&]{return table->rowCount()==1 && window.current().entries[0].progress.downloadedBytes>100000;});table->selectRow(0);
            if(window.current().entries[0].status!=downshift::QueueStatus::Running)
                std::cerr<<"Observed queue status="<<static_cast<int>(window.current().entries[0].status)<<" error="<<window.current().entries[0].error.toStdString()<<'\n';
            check(window.current().entries[0].status==downshift::QueueStatus::Running,"GUI must show real running worker");check(widget<QLabel>(window,"detail0")->text()==url,"Source details");
            check(window.grab().save(QDir(shots).filePath("light.bmp"),"BMP"),"Light render");
            widget<QPushButton>(window,"pauseButton")->click();wait([&]{return window.current().entries[0].status==downshift::QueueStatus::Paused;});check(widget<QPushButton>(window,"startButton")->isEnabled(),"Resume control");
            widget<QLineEdit>(window,"searchField")->setText("absent-search");check(table->isRowHidden(0),"Search hides mismatch");widget<QLineEdit>(window,"searchField")->clear();table->selectRow(0);
            widget<QPushButton>(window,"startButton")->click();wait([&]{return window.current().entries[0].status==downshift::QueueStatus::Completed;});
            measuring=false;
            check(window.current().entries[0].progress.downloadedBytes==2*1024*1024,"Completed byte count");check(!widget<QPushButton>(window,"startButton")->isEnabled(),"Completed control disabled");
            widget<QComboBox>(window,"filterField")->setCurrentIndex(3);check(table->isRowHidden(0),"Attention filter");widget<QComboBox>(window,"filterField")->setCurrentIndex(2);check(!table->isRowHidden(0),"History filter");table->selectRow(0);
            widget<QPushButton>(window,"settingsButton")->click();auto* settings=widget<QDialog>(window,"settingsDialog");widget<QLineEdit>(*settings,"settingsDirectory")->setText(root);widget<QComboBox>(*settings,"settingsTheme")->setCurrentIndex(1);widget<QSpinBox>(*settings,"settingsSimultaneous")->setValue(2);widget<QCheckBox>(*settings,"settingsNotifications")->setChecked(false);widget<QCheckBox>(*settings,"settingsDiagnostics")->setChecked(true);widget<QPushButton>(*settings,"settingsSave")->click();
            wait([&]{return window.current().settings.dark;});check(window.current().settings.simultaneous==2,"Settings connected");check(window.grab().save(QDir(shots).filePath("dark.bmp"),"BMP"),"Dark render");
            widget<QPushButton>(window,"settingsButton")->click();settings=widget<QDialog>(window,"settingsDialog");widget<QComboBox>(*settings,"settingsTheme")->setCurrentIndex(0);settings->reject();check(window.current().settings.dark,"Cancel preserves settings");
            window.openAdd();add=widget<QDialog>(window,"addDialog");widget<QLineEdit>(*add,"addUrl")->setText("file:///C:/private");widget<QLineEdit>(*add,"addDestination")->setText(QDir(root).filePath("invalid.bin"));widget<QPushButton>(*add,"addSubmit")->click();wait([&]{return widget<QLabel>(window,"banner")->isVisible() && widget<QLabel>(window,"banner")->property("error").toBool();});check(window.current().entries.size()==1,"Invalid input creates no download");add->reject();
            check(beats>200 && maximumGap<1.0,"Event loop responsiveness during actual transfer");
            std::filesystem::copy_file(std::filesystem::path(file.toStdWString()),std::filesystem::path(QDir(root).filePath("proof.bin").toStdWString()));
        }
        {
            downshift::QueueManager queue(database);downshift::MainWindow window(queue);window.show();wait([&]{return window.ready();});check(window.current().settings.dark && window.current().entries.size()==1,"Settings/history reopening");check(window.current().entries[0].status==downshift::QueueStatus::Completed,"Completed history reopening");check(window.current().entries[0].progress.averageBytesPerSecond>0 && window.current().entries[0].progress.elapsedSeconds>0,"History retains worker metrics");
            auto future=queue.deleteCompleted(window.current().entries[0].id);wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;});future.get();wait([&]{return window.current().entries.empty();});check(!QFileInfo::exists(file),"Delete worker removes completed file");
        }
        std::cout<<"GUI checks passed: "<<checks<<"; heartbeat ticks="<<beats<<"; longest event gap="<<maximumGap<<" seconds\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"GUI test failed: "<<e.what()<<"\n";return 1;}
}
