#include "NativeProtocol.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#ifdef DOWNSHIFT_TEST_HOST
#include <QFile>
#endif
namespace {
bool read(void* data,DWORD size){DWORD done=0;auto* bytes=static_cast<char*>(data);while(done<size){DWORD n=0;if(!ReadFile(GetStdHandle(STD_INPUT_HANDLE),bytes+done,size-done,&n,nullptr) || !n)return false;done+=n;}return true;}
bool write(const QByteArray& data){const auto n=static_cast<unsigned>(data.size());unsigned char prefix[4];for(unsigned i=0;i<4;++i)prefix[i]=static_cast<unsigned char>(n>>(8*i));DWORD done=0;if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),prefix,4,&done,nullptr) || done!=4)return false;return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),data.constData(),n,&done,nullptr) && done==n;}
void launch(){
#ifdef DOWNSHIFT_TEST_HOST
// A private cold-start fixture retains the parent's controllable stdio when
// starting its hidden app. This hook is absent from the production host.
const auto signal=qEnvironmentVariable("DOWNSHIFT_COLD_TEST_SIGNAL");
if(!signal.isEmpty()){
    const QFileInfo info(signal);
    if(!info.isAbsolute() || info.fileName()!=".native-start-request" || !QFileInfo::exists(info.absoluteDir().filePath(".native-launch-fixture")))throw std::runtime_error("Invalid isolated startup fixture.");
    QFile file(signal);if(!file.exists()){if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Cannot signal isolated startup.");file.write("open");}return;
}
throw std::runtime_error("The isolated test app is unavailable.");
#else
wchar_t module[32768]{};if(!GetModuleFileNameW(nullptr,module,32768))throw std::runtime_error("Downshift could not be opened.");const auto exe=QDir(QFileInfo(QString::fromWCharArray(module)).absolutePath()).filePath("Downshift.exe").toStdWString();std::wstring command=L"\""+exe+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))throw std::runtime_error("Downshift could not be opened.");CloseHandle(process.hThread);CloseHandle(process.hProcess);
#endif
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);using namespace downshift::browser;
    const auto arguments=app.arguments();
    const bool chromium=arguments.value(1)=="chrome-extension://"+QString::fromLatin1(extensionId)+"/";
    const bool firefox=arguments.size()>=3 && arguments[2]==QString::fromLatin1(firefoxExtensionId) &&
        QFileInfo(arguments[1]).isAbsolute() && QFileInfo(arguments[1]).suffix()=="json";
    if(!chromium && !firefox)return 2;
#ifdef DOWNSHIFT_TEST_HOST
    useIsolatedTestPipe();
#endif
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    unsigned char prefix[4];
    while(read(prefix,4)) {
        unsigned n=0;for(unsigned i=0;i<4;++i)n|=static_cast<unsigned>(prefix[i])<<(8*i);if(!n || n>maximumMessage)return 3;
        QByteArray data(static_cast<qsizetype>(n),'\0');if(!read(data.data(),n))return 3;QJsonObject response;
        try {
            const auto request=parse(data);validate(request);
            if(request.value("command")=="open") {try{response=send(request);}catch(...){launch();response={{"ok",true},{"state","opening"},{"version",1}};}}
            else response=send(request);
        }catch(const std::exception& e){response={{"ok",false},{"error",QString::fromUtf8(e.what())},{"version",1}};}
        if(!write(QJsonDocument(response).toJson(QJsonDocument::Compact)))return 4;
    }return 0;
}
