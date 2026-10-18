#include "TorrentFixture.h"
#include "torrent/StallRecovery.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
using namespace t4;
namespace {
void idle(int milliseconds){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);while(std::chrono::steady_clock::now()<end){QCoreApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}}
struct Bridge final:QObject {
    QTcpSocket* client;
    QTcpSocket* upstream;
    bool wedged=false;
    Bridge(QTcpSocket* socket,quint16 port,bool offline,QObject* parent):QObject(parent),client(socket),upstream(new QTcpSocket(this)),wedged(offline){
        client->setParent(this);
        QObject::connect(client,&QTcpSocket::readyRead,this,[this]{const auto data=client->readAll();if(!wedged)upstream->write(data);});
        upstream->setReadBufferSize(32768);
        client->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption,4096);
        QObject::connect(upstream,&QTcpSocket::readyRead,this,[this]{if(wedged)upstream->readAll();});
        auto* pump=new QTimer(this);pump->setInterval(20);
        QObject::connect(pump,&QTimer::timeout,this,[this]{if(!wedged&&client->bytesToWrite()<8192)client->write(upstream->read(1024));});pump->start();
        QObject::connect(client,&QTcpSocket::disconnected,this,[this]{upstream->abort();});
        QObject::connect(upstream,&QTcpSocket::disconnected,this,[this]{client->disconnectFromHost();});
        upstream->connectToHost(QHostAddress::LocalHost,port);
    }
};
struct Link final:QObject {
    QTcpServer server;
    std::vector<Bridge*> bridges;
    quint16 donor;
    bool offline=false;
    explicit Link(quint16 port):donor(port){
        require(server.listen(QHostAddress::LocalHost),"Private peer relay listens");
        QObject::connect(&server,&QTcpServer::newConnection,this,[this]{while(server.hasPendingConnections())bridges.push_back(new Bridge(server.nextPendingConnection(),donor,offline,this));});
    }
    void loseNetwork(){offline=true;for(auto* bridge:bridges)bridge->wedged=true;}
    // New connections work again. Existing half-open flows remain blackholed.
    void restoreNetwork(){offline=false;}
};
void policy(){
    using Recovery=downshift::torrent::StallRecovery;using Action=Recovery::Action;
    const auto start=Recovery::Clock::now();Recovery value;
    require(value.observe(start,true,100,0)==Action::None,"Active progress arms recovery");
    require(value.observe(start+std::chrono::seconds(44),true,100,0)==Action::None,"Short zero-rate sample does not interrupt peers");
    require(value.observe(start+std::chrono::seconds(45),true,100,0)==Action::Discover,"Sustained idle first refreshes discovery");
    require(value.observe(start+std::chrono::seconds(89),true,100,0)==Action::None,"Peer connection grace period is respected");
    for(const int second:{90,180,360,720,1320})require(value.observe(start+std::chrono::seconds(second),true,100,0)==Action::Reconnect,"Repeated outage retries back off and remain bounded");
    require(value.observe(start+std::chrono::seconds(1321),true,101,0)==Action::None,"One received byte resets recovery even before any whole piece verifies");
    require(value.observe(start+std::chrono::seconds(1365),true,101,0)==Action::None,"Slow partial-piece traffic gets a fresh idle grace period");
    require(value.observe(start+std::chrono::seconds(2000),false,101,0)==Action::None,"Paused/completed/preview eligibility disarms recovery");
    require(value.observe(start+std::chrono::seconds(3000),true,101,0)==Action::None,"Explicit restart begins with a full grace period");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir temporary;
try{
    policy();require(temporary.isValid(),"Private recovery fixture");const auto root=temporary.path();
    const auto metadata=create(root,lt::create_torrent::v1_only,"recovering");Seed seed(root,metadata);Link link(seed.port());
    QTcpServer tracker;require(tracker.listen(QHostAddress::LocalHost),"Private tracker listens");int announces=0;
    QObject::connect(&tracker,&QTcpServer::newConnection,[&]{while(tracker.hasPendingConnections()){
        auto* socket=tracker.nextPendingConnection();QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
            auto request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);
            if(!request.contains("\r\n\r\n")||socket->property("answered").toBool())return;
            socket->setProperty("answered",true);++announces;const auto port=link.server.serverPort();
            QByteArray peer;peer.append(char(127));peer.append(char(0));peer.append(char(0));peer.append(char(1));peer.append(char(port>>8));peer.append(char(port&255));
            const QByteArray body="d8:intervali1800e12:min intervali1800e5:peers6:"+peer+"e";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);socket->disconnectFromHost();
        });
    }});
    auto uri=QString::fromStdString(lt::make_magnet_uri(lt::load_torrent_file(metadata.toUtf8().toStdString())));
    uri+="&tr="+QString::fromLatin1(QUrl::toPercentEncoding(QString("http://127.0.0.1:%1/announce").arg(tracker.serverPort())));
    TorrentEngine engine({false,true,QDir(root).filePath("recovery.sqlite")});AddOptions options;options.metadataOnly=true;options.limits={64*1024,32*1024,3};
    const auto destination=QDir(root).filePath("payload");const auto id=engine.addMagnet(uri,destination,options).get();
    wait([&]{auto value=row(engine,id);return value.metadata&&!value.selectionPending&&value.state==State::Paused;},20);
    auto accepted=link.bridges.size();idle(3700);require(row(engine,id).metadataOnly&&row(engine,id).downloadedBytes==0&&link.bridges.size()==accepted,"File-selection preview never auto-starts during prolonged idle");
    engine.startSelected(id,false).get();wait([&]{return row(engine,id).downloadedBytes>65536;},20);
    const auto healthyMetadata=create(root,lt::create_torrent::v1_only,"healthy");Seed healthySeed(root,healthyMetadata);
    const auto healthy=engine.addFile(healthyMetadata,QDir(root).filePath("healthy-payload")).get();engine.connectPeer(healthy,"127.0.0.1",healthySeed.port()).get();
    bool watch=false,unexpectedPause=false;QObject receiver;
    QObject::connect(&engine,&TorrentEngine::updated,&receiver,[&](Snapshot value){if(watch&&value.id==id&&value.state==State::Paused)unexpectedPause=true;},Qt::QueuedConnection);
    for(int outage=0;outage<2;++outage){
        require(row(engine,id).state==State::Downloading,"Transfer is incomplete before controlled outage");
        link.loseNetwork();idle(1200);const auto stalled=row(engine,id);accepted=link.bridges.size();link.restoreNetwork();watch=true;
        wait([&]{return link.bridges.size()>accepted&&row(engine,id).downloadedBytes>stalled.downloadedBytes+16384;},15);
        require(!unexpectedPause,"Automatic reconnect preserves running intent without a user Pause/Resume command");
        const auto recovered=row(engine,id);require(recovered.limits.download==64*1024&&recovered.limits.upload==32*1024&&recovered.limits.connections==3,"Recovery retains per-task speed and connection caps");
        require(recovered.verifiedBytes>=stalled.verifiedBytes,"Automatic reconnect retains already verified pieces");
        std::cout<<"Recovered outage "<<outage+1<<"; accepted peer connections="<<link.bridges.size()<<std::endl;
    }
    watch=false;engine.pause(id).get();accepted=link.bridges.size();const auto stopped=row(engine,id);idle(3700);
    require(row(engine,id).state==State::Paused&&row(engine,id).nativePaused&&link.bridges.size()==accepted,"Explicit user Pause remains paused across recovery deadlines");
    require(row(engine,id).verifiedBytes>=stopped.verifiedBytes,"Paused file progress never goes backwards while queued writes finish");
    engine.resume(id).get();wait([&]{return row(engine,id).state==State::Complete;},35);
    wait([&]{return row(engine,healthy).state==State::Complete;});require(!unexpectedPause,"Other torrent continues independently during reconnects");
    for(const auto& file:row(engine,id).files)require(digest(QDir(destination).filePath(file.path))==digest(QDir(root).filePath("seed/"+file.path)),"Recovered magnet payload has exact final SHA-256");
    // A socket accepted late by Qt can belong to an earlier connection attempt.
    // Check settled native peer/payload state instead of counting accept events.
    wait([&]{const auto value=row(engine,id);return value.state==State::Complete&&value.nativePaused&&value.peers==0;});
    const auto completed=row(engine,id);idle(3700);const auto afterCompletion=row(engine,id);
    require(afterCompletion.state==State::Complete&&afterCompletion.nativePaused&&afterCompletion.peers==0
        &&afterCompletion.downloadedBytes==completed.downloadedBytes&&afterCompletion.uploadedBytes==completed.uploadedBytes,"Completed stop-policy torrent stays paused without payload transfer or seeding");
    engine.shutdown().get();std::cout<<"Torrent recovery checks passed: "<<checks<<"; tracker announces="<<announces<<"; private peer relay only; idle clock accelerated in this test target"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
