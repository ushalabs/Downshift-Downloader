#include "PipeServer.h"
#include "NativeProtocol.h"
#include "core/DownloadQueue/VideoUrl.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>
#include <QJsonDocument>
#include <array>
namespace downshift::browser {
namespace {
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE && value)CloseHandle(value);}};
bool finish(HANDLE pipe,OVERLAPPED& operation,DWORD& bytes,std::stop_token stop,DWORD timeout) {
    const auto start=GetTickCount64();
    while(!stop.stop_requested() && GetTickCount64()-start<timeout) if(WaitForSingleObject(operation.hEvent,25)==WAIT_OBJECT_0)return GetOverlappedResult(pipe,&operation,&bytes,FALSE)!=0;
    CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&bytes,TRUE);return false;
}
bool io(HANDLE pipe,void* data,DWORD size,bool write,std::stop_token stop) {
    Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};if(!event.value)return false;OVERLAPPED operation{};operation.hEvent=event.value;DWORD count=0;
    const auto ok=write ? WriteFile(pipe,data,size,&count,&operation):ReadFile(pipe,data,size,&count,&operation);
    if(!ok && (GetLastError()!=ERROR_IO_PENDING || !finish(pipe,operation,count,stop,10000)))return false;
    return count==size;
}
}
PipeServer::PipeServer(QueueManager& queue,std::function<void()> open,std::function<void(QString,QString)> media,std::function<void()> blocked):queue_(queue),open_(std::move(open)),media_(std::move(media)),blocked_(std::move(blocked)),thread_([this](std::stop_token stop){run(stop);}){}
PipeServer::~PipeServer(){thread_.request_stop();}
void PipeServer::run(std::stop_token stop) {
    try {
        PSECURITY_DESCRIPTOR descriptor=nullptr;const auto policy=("D:P(A;;GA;;;"+userSid()+")(A;;GA;;;SY)").toStdWString();
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(policy.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return;
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};const auto name=pipeName().toStdWString();
        Handle pipe{CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,maximumMessage+4,maximumMessage+4,1000,&security)};
        LocalFree(descriptor);if(pipe.value==INVALID_HANDLE_VALUE)return;active_=true;
        while(!stop.stop_requested()) {
            Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};OVERLAPPED connection{};connection.hEvent=event.value;DWORD count=0;
            const auto connected=ConnectNamedPipe(pipe.value,&connection);const auto error=GetLastError();
            if(!connected && error!=ERROR_PIPE_CONNECTED && (error!=ERROR_IO_PENDING || !finish(pipe.value,connection,count,stop,1000)))continue;
            try {
                ULONG pid=0;if(!GetNamedPipeClientProcessId(pipe.value,&pid) || !sameUserProcess(pid))throw std::runtime_error("Untrusted local client.");
                // Message-mode preserves the entire bounded request, including its length prefix.
                std::array<char,maximumMessage+4> buffer{};Handle readEvent{CreateEventW(nullptr,TRUE,FALSE,nullptr)};OVERLAPPED read{};read.hEvent=readEvent.value;DWORD received=0;
                const auto ok=ReadFile(pipe.value,buffer.data(),static_cast<DWORD>(buffer.size()),&received,&read);
                if(!ok && (GetLastError()!=ERROR_IO_PENDING || !finish(pipe.value,read,received,stop,10000)))throw std::runtime_error("Invalid local frame.");
                if(received<4)throw std::runtime_error("Invalid local frame.");unsigned length=0;for(unsigned i=0;i<4;++i)length|=static_cast<unsigned char>(buffer[i])<<(8*i);
                if(!length || length>maximumMessage || received!=length+4)throw std::runtime_error("Invalid local frame.");
                QJsonObject response;
                try{response=handle(parse(QByteArray(buffer.data()+4,static_cast<int>(length))),stop);response["ok"]=true;}
                catch(const std::exception& e){response={{"ok",false},{"error",QString::fromUtf8(e.what())}};}
                response["version"]=1;const auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact);QByteArray frame(4,'\0');const auto n=static_cast<unsigned>(bytes.size());for(unsigned i=0;i<4;++i)frame[i]=static_cast<char>(n>>(8*i));frame.append(bytes);io(pipe.value,frame.data(),static_cast<DWORD>(frame.size()),true,stop);
            }catch(...){}DisconnectNamedPipe(pipe.value);
        }
    }catch(...){}active_=false;
}
QJsonObject PipeServer::handle(const QJsonObject& request,std::stop_token stop) {
    validate(request);const auto command=request.value("command").toString();
    if(command=="ping"){(void)queue_.snapshot().get();return {{"state","ready"}};}
    if(command=="open"){open_();return {{"state","ready"}};}
    if(command=="blocked"){open_();if(blocked_)blocked_();return {{"state","blocked"}};}
    if(command=="media"){
        if(!media_)throw std::runtime_error("Video downloads are unavailable in this application.");
        media_(request.value("url").toString(),request.value("title").toString());return {{"state","video-dialog"}};
    }
    const auto key=request.value("key").toString();
    if(command=="prepare") {
        if(youtubeHost(QUrl(request.value("url").toString()).host()))throw std::runtime_error("Use the YouTube player's Download this video action; a watch page is not a video file.");
        const auto old=queue_.handoff(key,"status").get();if(old.value("state")!="unknown")return queue_.prepare(key,request.value("url").toString(),request.value("filename").toString(),{}).get();
        const auto url=request.value("url").toString();const auto inspection=DownloadEngine().inspect(url.toStdString(),stop);
        const auto expected=request.value("total").toString().toLongLong();
        if(inspection.total && request.contains("total") && expected>=0 && static_cast<std::uint64_t>(expected)!=inspection.total)throw std::runtime_error("The public file size differs from Chrome's download.");
        const auto mime=request.value("mime").toString();
        if((inspection.contentType.starts_with("text/html") || inspection.contentType.starts_with("application/xhtml")) && !mime.startsWith("text/html") && !mime.startsWith("application/xhtml"))throw std::runtime_error("The server returned a webpage instead of this file. Authentication may be required.");
        return queue_.prepare(key,url,request.value("filename").toString(),inspection,mime).get();
    }
    return queue_.handoff(key,command,request.value("prompt").toBool()).get();
}
}
