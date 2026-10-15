#include "core/FileWriter/FileWriter.h"
#include "core/ResumeManager/ResumeJournal.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>
using namespace downshift;
namespace {
int checks=0;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;}
std::filesystem::path path(const QString& text){return std::filesystem::path(text.toStdWString());}
void seed(const QString& target,RecoveryStatus status,bool mismatch=false){
    FileWriter file(path(target));file.writeAt(0,"incomplete",10);file.flush();RecoveryRecord record;record.id=ResumeJournal::newId();record.url="https://example.org/fixture";record.destination=ResumeJournal::pathText(path(target));record.fileIdentity=mismatch?"0:0:0":file.identity();record.status=status;ResumeJournal journal(path(target));journal.save(record);
}
template<class F>void refused(F action){bool rejected=false;try{action();}catch(...){rejected=true;}check(rejected,"Unsafe cleanup must be rejected");}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        if(app.arguments().size()==3&&app.arguments()[1]=="--apply-plan"){
            QFile plan(app.arguments()[2]);check(plan.open(QIODevice::ReadOnly),"Explicit cleanup plan opens");const auto entries=QJsonDocument::fromJson(plan.readAll()).array();check(!entries.isEmpty()&&entries.size()<=10,"Bounded explicit cleanup plan");const auto downloads=QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).canonicalPath();int removed=0;
            for(const auto& entry:entries){const auto target=entry.toString();const auto parent=QFileInfo(target).absoluteDir().canonicalPath();check(!target.isEmpty()&&!downloads.isEmpty()&&parent.startsWith(downloads+"/",Qt::CaseInsensitive),"Cleanup stays inside the named Downloads directory");for(auto folder=parent;folder!=downloads;folder=QFileInfo(folder).absolutePath()){QFileInfo info(folder);check(!info.isSymLink()&&!info.isJunction(),"No linked cleanup parents");}ResumeJournal journal(path(target));journal.discardFailedPartial();++removed;}
            std::cout<<"Authenticated failed fragments removed: "<<removed<<std::endl;return 0;
        }
        QTemporaryDir temporary;check(temporary.isValid(),"Private cleanup fixtures");const auto file=QDir(temporary.path()).filePath("failed.bin");seed(file,RecoveryStatus::Failed);
        {QFile final(file);check(final.open(QIODevice::WriteOnly),"Independent completed destination fixture");final.write("completed file stays");}
        {ResumeJournal journal(path(file));journal.discardFailedPartial();}check(QFileInfo::exists(file)&&!QFileInfo::exists(file+".part")&&!QFileInfo::exists(file+".part.resume"),"Failed partial cleanup preserves completed destination");
        const auto paused=QDir(temporary.path()).filePath("paused.bin");seed(paused,RecoveryStatus::Paused);{ResumeJournal journal(path(paused));refused([&]{journal.discardFailedPartial();});}check(QFileInfo::exists(paused+".part"),"Paused partial preserved");
        const auto changed=QDir(temporary.path()).filePath("changed.bin");seed(changed,RecoveryStatus::Failed,true);{ResumeJournal journal(path(changed));refused([&]{journal.discardFailedPartial();});}check(QFileInfo::exists(changed+".part")&&QFileInfo::exists(changed+".part.resume"),"Mismatched ownership preserves evidence");
        const auto archived=QDir(temporary.path()).filePath("archived.bin");seed(archived,RecoveryStatus::Failed);{ResumeJournal journal(path(archived));journal.archivePrevious();}const auto names=QDir(temporary.path()).entryList({"Downshift.previous-*.part"},QDir::Files);check(names.size()==1,"Legacy archive fixture");const auto archive=QDir(temporary.path()).filePath(names.first().chopped(5));{ResumeJournal journal(path(archive));journal.discardFailedPartial();}check(!QFileInfo::exists(archive+".part")&&!QFileInfo::exists(archive+".part.resume"),"Authenticated failed archive cleanup");
        std::cout<<"Owned cleanup checks passed: "<<checks<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<"Owned cleanup: "<<error.what()<<std::endl;return 1;}
}
