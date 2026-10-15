#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>
#include <iostream>
// Test-only native crash diagnostics; never installs a machine-wide handler.
inline LONG WINAPI recordTestCrash(EXCEPTION_POINTERS* exception){
    const auto process=GetCurrentProcess();SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);SymInitialize(process,nullptr,TRUE);
    auto context=*exception->ContextRecord;STACKFRAME64 frame{};frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};
    std::cerr<<"Native test exception: "<<std::hex<<exception->ExceptionRecord->ExceptionCode<<std::dec<<std::endl;
    for(int n=0;n<40;++n){
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;DWORD64 displacement=0;
        if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))std::cerr<<symbol->Name<<" + "<<displacement;
        else std::cerr<<"address 0x"<<std::hex<<frame.AddrPC.Offset<<std::dec;
        IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD offset=0;if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&offset,&line))std::cerr<<" ("<<line.FileName<<":"<<line.LineNumber<<")";std::cerr<<std::endl;
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }
    SymCleanup(process);return EXCEPTION_EXECUTE_HANDLER;
}
