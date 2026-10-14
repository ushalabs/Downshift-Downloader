#include "torrent/TorrentEngine.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("DownshiftTorrent");
    QCommandLineParser parser;parser.setApplicationDescription("Downshift Torrent T1 developer tool. Peers can see your IP; uploads may occur while downloading. Stops at verified completion. No persistent torrent recovery yet.");parser.addHelpOption();
    parser.addOption({"torrent","Local torrent metadata.","file"});parser.addOption({"magnet","Magnet URI.","uri"});parser.addOption({"save","Absolute destination directory.","directory"});parser.addOption({"timeout","Maximum seconds to wait (1-86400).","seconds","600"});parser.process(app);
    bool ok=false;const auto timeout=parser.value("timeout").toInt(&ok);
    if(!ok||timeout<1||timeout>86400||parser.isSet("torrent")==parser.isSet("magnet")||!parser.isSet("save")){std::cerr<<"Use exactly one of --torrent/--magnet, plus --save.\n";return 2;}
    try{
        downshift::torrent::TorrentEngine engine;
        const auto id=(parser.isSet("torrent")?engine.addFile(parser.value("torrent"),parser.value("save")):engine.addMagnet(parser.value("magnet"),parser.value("save"))).get();
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);int result=3;
        while(std::chrono::steady_clock::now()<end){
            auto rows=engine.snapshots().get();for(const auto& row:rows)if(row.id==id){
                QJsonObject report{{"state",static_cast<int>(row.state)},{"metadata",row.metadata},{"verifiedBytes",QString::number(row.verifiedBytes)},{"totalBytes",QString::number(row.totalBytes)},{"downloadBytesPerSecond",row.downloadBytesPerSecond},{"peers",row.peers}};
                std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).constData()<<std::endl;
                if(row.state==downshift::torrent::State::Complete)result=0;
                if(row.state==downshift::torrent::State::Failed){std::cerr<<row.error.toStdString()<<'\n';result=1;}
            }
            if(result!=3)break;std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        engine.shutdown().get();if(result==3)std::cerr<<"Timed out waiting for peers/metadata or completion. Input is not necessarily invalid. Data retained; persistent recovery is not implemented in T1.\n";
        return result;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
