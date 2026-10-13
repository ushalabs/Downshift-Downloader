#include "NativeProtocol.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace downshift::browser {
namespace {
bool isolatedTestPipe=false;
[[noreturn]] void fail(){throw std::runtime_error("Downshift's browser connection is unavailable or the request is invalid. Open the updated app and reload its extension.");}
QString sid(HANDLE process) {
    HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))fail();DWORD count=0;GetTokenInformation(token,TokenUser,nullptr,0,&count);std::vector<unsigned char> buffer(count);
    const auto ok=GetTokenInformation(token,TokenUser,buffer.data(),count,&count);CloseHandle(token);if(!ok)fail();wchar_t* text=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&text))fail();const auto result=QString::fromWCharArray(text);LocalFree(text);return result;
}
bool hexadecimal(const QString& key){return key.size()==32 && std::all_of(key.begin(),key.end(),[](QChar c){return QString("0123456789abcdef").contains(c);});}
}
QString userSid(){return sid(GetCurrentProcess());}
bool sameUserProcess(unsigned long id){const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);if(!process)return false;bool same=false;try{same=sid(process)==userSid();}catch(...){}CloseHandle(process);return same;}
QString pipeName(){return "\\\\.\\pipe\\Downshift-"+userSid()+(isolatedTestPipe ? "-tests":"");}
void useIsolatedTestPipe(){isolatedTestPipe=true;}
QJsonObject parse(const QByteArray& data) {if(data.isEmpty() || data.size()>maximumMessage)fail();QJsonParseError error{};const auto doc=QJsonDocument::fromJson(data,&error);if(error.error!=QJsonParseError::NoError || !doc.isObject())fail();return doc.object();}
void validate(const QJsonObject& request) {
    if(request.value("version")!=QJsonValue(1) || !request.value("command").isString())fail();const auto command=request.value("command").toString();QSet<QString> allowed={"version","command"};
    if(command=="prepare"){
        allowed.unite({"key","url","filename","total","mime","serverFilename"});if(!hexadecimal(request.value("key").toString()))fail();
        if(request.contains("serverFilename") && !request.value("serverFilename").isBool())fail();
        if(request.contains("mime") && (!request.value("mime").isString() || request.value("mime").toString().size()>120 || request.value("mime").toString().contains(QChar(0))))fail();
        const auto url=request.value("url").toString(),name=request.value("filename").toString();if(url.isEmpty() || url.size()>32768 || url.contains(QChar(0)) || name.isEmpty() || name.size()>160 || name.contains(QChar(0)))fail();
        if(request.contains("total")){bool valid=false;const auto n=request.value("total").toString().toLongLong(&valid);if(!valid || n < -1)fail();}
    }else if(command=="media"){
        allowed.unite({"url","title"});const auto address=request.value("url").toString();const QUrl url(address);
        if(address.isEmpty() || address.size()>32768 || address.contains(QChar(0)) || !url.isValid() || (url.scheme()!="https" && url.scheme()!="http") || url.host().isEmpty() || !url.userInfo().isEmpty())fail();
        if(!request.value("title").isString() || request.value("title").toString().size()>200 || request.value("title").toString().contains(QChar(0)))fail();
    }else if(command=="commit" || command=="abort" || command=="status"){
        allowed.insert("key");if(!hexadecimal(request.value("key").toString()))fail();
        if(command=="commit"){
            allowed.unite({"browserCancelled","browserNotStarted","prompt"});
            const bool cancelled=request.value("browserCancelled")==QJsonValue(true),notStarted=request.value("browserNotStarted")==QJsonValue(true);
            if(cancelled==notStarted || (request.contains("browserCancelled") && !request.value("browserCancelled").isBool()) || (request.contains("browserNotStarted") && !request.value("browserNotStarted").isBool()) || (request.contains("prompt") && !request.value("prompt").isBool()))fail();
        }
    }else if(command!="ping" && command!="open" && command!="blocked")fail();
    for(auto i=request.begin();i!=request.end();++i)if(!allowed.contains(i.key()))fail();
}
QJsonObject send(const QJsonObject& request) {
    validate(request);const auto name=pipeName().toStdWString();
    if(!WaitNamedPipeW(name.c_str(),1500))fail();const auto pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(pipe==INVALID_HANDLE_VALUE)fail();
    try {
        ULONG pid=0;if(!GetNamedPipeServerProcessId(pipe,&pid) || !sameUserProcess(pid))fail();
        const auto data=QJsonDocument(request).toJson(QJsonDocument::Compact);std::vector<unsigned char> frame(data.size()+4);const auto n=static_cast<unsigned>(data.size());for(unsigned i=0;i<4;++i)frame[i]=static_cast<unsigned char>(n>>(8*i));std::memcpy(frame.data()+4,data.constData(),data.size());
        DWORD mode=PIPE_READMODE_MESSAGE;if(!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr))fail();
        std::vector<char> response(maximumMessage+4);DWORD count=0;
        const auto event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event)fail();OVERLAPPED operation{};operation.hEvent=event;
        bool ok=TransactNamedPipe(pipe,frame.data(),static_cast<DWORD>(frame.size()),response.data(),static_cast<DWORD>(response.size()),&count,&operation)!=0;
        if(!ok && GetLastError()==ERROR_IO_PENDING) {
            if(WaitForSingleObject(event,12000)==WAIT_OBJECT_0)ok=GetOverlappedResult(pipe,&operation,&count,FALSE)!=0;
            else{CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&count,TRUE);}
        }
        CloseHandle(event);if(!ok || count<4)fail();
        unsigned length=0;for(unsigned i=0;i<4;++i)length|=static_cast<unsigned char>(response[i])<<(8*i);if(length==0 || length>maximumMessage || count!=length+4)fail();
        const auto result=parse(QByteArray(response.data()+4,static_cast<int>(length)));CloseHandle(pipe);return result;
    }catch(...){CloseHandle(pipe);throw;}
}
}
