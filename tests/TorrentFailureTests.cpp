#include "TorrentFixture.h"
#include "TorrentFaultDisk.h"
using namespace t4;
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir temp;
try{
    require(temp.isValid(),"Private native disk-fault fixtures");const auto root=temp.path(),metadata=create(root,lt::create_torrent::v1_only,"disk-budget"),save=QDir(root).filePath("payload"),store=QDir(root).filePath("fault.sqlite");Seed seed(root,metadata);QString id;
    {downshift::torrent::testing::diskBudget(65536);TorrentEngine engine({false,true,store});id=engine.addFile(metadata,save).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{auto s=row(engine,id);return s.state==State::Failed;},30);require(!row(engine,id).error.isEmpty(),"ENOSPC is visible and never completed");wait([&]{return !QFileInfo::exists(QDir(save).filePath("disk-budget/file0.bin"));});engine.shutdown().get();}
    {downshift::torrent::testing::diskBudget(-1);TorrentEngine engine({false,true,store});require(row(engine,id).state==State::Failed,"Disk failure survives restart");engine.resume(id).get();engine.connectPeer(id,"127.0.0.1",seed.port()).get();wait([&]{return row(engine,id).state==State::Complete;},35);for(auto f:row(engine,id).files)require(digest(QDir(save).filePath(f.path))==digest(QDir(root).filePath("seed/"+f.path)),"Retry after disk failure has exact bytes");engine.shutdown().get();}
    std::cout<<"Native ENOSPC failure/restart/retry checks passed: "<<checks<<" (test-only native disk adapter; no physical disk filled)"<<std::endl;return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<std::endl;return 1;}}
