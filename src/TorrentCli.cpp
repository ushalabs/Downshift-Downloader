#include "torrent/TorrentEngine.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <chrono>
#include <iostream>
#include <thread>
using namespace downshift::torrent;
namespace {
QJsonObject report(const Snapshot& row){return {{"id",row.id},{"state",static_cast<int>(row.state)},{"metadata",row.metadata},{"metadataOnly",row.metadataOnly},{"verifyingCompletion",row.verifyingCompletion},{"nativePaused",row.nativePaused},{"uploadOnly",row.uploadOnly},{"selectionPending",row.selectionPending},{"metadataWaitSeconds",QString::number(row.metadataWaitSeconds)},{"dhtNodes",row.dhtNodes},{"candidatePeers",row.candidatePeers},{"connectingPeers",row.connectingPeers},{"respondingTrackers",row.respondingTrackers},{"failedTrackers",row.failedTrackers},{"trackerCount",row.trackerCount},{"downloadedBytes",QString::number(row.downloadedBytes)},{"verifiedBytes",QString::number(row.verifiedBytes)},{"totalBytes",QString::number(row.totalBytes)},{"selectedBytes",QString::number(row.selectedBytes)},{"downloadBytesPerSecond",row.downloadBytesPerSecond},{"uploadBytesPerSecond",row.uploadBytesPerSecond},{"peers",row.peers},{"seeds",row.seeds},{"ratio",row.ratio},{"seedingSeconds",QString::number(row.seedingSeconds)}};}
void output(const QJsonObject& object){std::cout<<QJsonDocument(object).toJson(QJsonDocument::Compact).constData()<<std::endl;}
int number(const QCommandLineParser& parser,const char* key){bool ok=false;const auto value=parser.value(key).toInt(&ok);if(!ok)throw std::runtime_error("Invalid integer option.");return value;}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("DownshiftTorrent");
    QCommandLineParser parser;parser.setApplicationDescription("Downshift torrent diagnostic tool. Peers can see your IP. Persistent sessions restore paused; use --resume with a saved ID.");parser.addHelpOption();
    parser.addOption({"metadata-only","Resolve the file list without requesting payload pieces."});
    parser.addOption({"torrent","Local torrent metadata.","file"});parser.addOption({"magnet","Magnet URI.","uri"});parser.addOption({"save","Absolute, separate destination for this torrent.","directory"});
    parser.addOption({"database","Absolute torrent SQLite store (separate from queue.sqlite).","file"});parser.addOption({"list","List retained torrent IDs/state."});parser.addOption({"id","Saved torrent ID for a management command.","id"});
    for(const auto* key:{"pause","resume","cancel","remove","delete-data","details","configure"})parser.addOption({key,QString("%1 the torrent selected by --id.").arg(key)});
    parser.addOption({"confirm-delete","Explicitly confirm deletion of this torrent's payload files."});parser.addOption({"paused","Add without starting."});parser.addOption({"priorities","Ordinary-file priorities: comma-separated 0=skip, 1..7.","values"});
    parser.addOption({"download-limit","Per-torrent download bytes/sec; 0 unlimited.","bytes","0"});parser.addOption({"upload-limit","Per-torrent upload bytes/sec; 0 unlimited.","bytes","262144"});parser.addOption({"connections","Per-torrent maximum peers (1..512).","count","64"});
    parser.addOption({"seed","Completion policy: stop, ratio, time or manual.","policy","stop"});parser.addOption({"seed-ratio","Upload/download ratio target.","ratio","1"});parser.addOption({"seed-seconds","Active post-completion seeding seconds.","seconds","3600"});
    parser.addOption({"global-limits","Apply download/upload/connections options session-wide."});parser.addOption({"timeout","Maximum seconds to wait (1-86400).","seconds","600"});parser.process(app);
    try{
        const auto timeout=number(parser,"timeout");if(timeout<1||timeout>86400)throw std::runtime_error("Invalid timeout.");
        const bool adding=parser.isSet("torrent")||parser.isSet("magnet");int actions=0;QString action;
        for(const auto* key:{"pause","resume","cancel","remove","delete-data","details","configure"})if(parser.isSet(key)){++actions;action=key;}
        if((adding&&(parser.isSet("torrent")==parser.isSet("magnet")||!parser.isSet("save")||actions||parser.isSet("id")))||actions>1||(!adding&&!parser.isSet("list")&&!parser.isSet("global-limits")&&actions==0)||(actions&&(!parser.isSet("id")||!parser.isSet("database")))){std::cerr<<"Use --torrent/--magnet with --save, or --database with --list/--id and one management action.\n";return 2;}
        if(parser.isSet("metadata-only")&&!adding){std::cerr<<"--metadata-only requires a new --magnet or --torrent lookup.\n";return 2;}
        AddOptions config;config.paused=parser.isSet("paused");config.metadataOnly=parser.isSet("metadata-only");config.limits={number(parser,"download-limit"),number(parser,"upload-limit"),number(parser,"connections")};
        const auto policy=parser.value("seed");if(policy=="stop")config.seeding.mode=SeedMode::Stop;else if(policy=="ratio")config.seeding.mode=SeedMode::Ratio;else if(policy=="time")config.seeding.mode=SeedMode::Time;else if(policy=="manual")config.seeding.mode=SeedMode::Manual;else throw std::runtime_error("Invalid seed policy.");
        bool ok=false;config.seeding.ratio=parser.value("seed-ratio").toDouble(&ok);if(!ok)throw std::runtime_error("Invalid seed ratio.");config.seeding.seconds=number(parser,"seed-seconds");
        if(parser.isSet("priorities"))for(const auto& value:parser.value("priorities").split(',')){const auto priority=value.toInt(&ok);if(!ok)throw std::runtime_error("Invalid file priority.");config.priorities.push_back(priority);}
        TorrentEngine engine({true,false,parser.value("database")});
        if(parser.isSet("global-limits"))engine.setSessionLimits(config.limits).get();
        auto id=parser.value("id");
        if(adding)id=(parser.isSet("torrent")?engine.addFile(parser.value("torrent"),parser.value("save"),config):engine.addMagnet(parser.value("magnet"),parser.value("save"),config)).get();
        if(action=="pause")engine.pause(id).get();else if(action=="resume")engine.resume(id).get();else if(action=="cancel")engine.cancel(id).get();else if(action=="remove")engine.remove(id).get();else if(action=="delete-data")engine.remove(id,true,parser.isSet("confirm-delete")).get();
        else if(action=="configure"){engine.configure(id,config.limits,config.seeding).get();if(parser.isSet("priorities"))engine.selectFiles(id,config.priorities).get();}
        else if(action=="details"){
            const auto detail=engine.details(id).get();QJsonArray trackers,peers;for(const auto& item:detail.trackers)trackers.append(QJsonObject{{"url",item.url},{"error",item.error},{"failures",item.failures}});for(const auto& item:detail.peers)peers.append(QJsonObject{{"address",item.address},{"client",item.client},{"download",item.download},{"upload",item.upload}});output({{"trackers",trackers},{"peers",peers},{"pieces",static_cast<int>(detail.availability.size())}});
        }
        const bool wait=(adding&&!config.paused)||action=="resume";int result=wait?3:0;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);
        do{
            const auto rows=engine.snapshots().get();for(const auto& row:rows)if(!wait||row.id==id){output(report(row));if(wait&&(row.state==State::Complete||(config.metadataOnly&&row.metadata)))result=0;if(wait&&row.state==State::Failed){std::cerr<<row.error.toStdString()<<'\n';result=1;}}
            if(!wait||result!=3)break;std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }while(std::chrono::steady_clock::now()<end);
        engine.shutdown().get();if(result==3)std::cerr<<"Bounded wait expired. Files and committed recovery data retained; persistent sessions reopen paused.\n";return result;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
