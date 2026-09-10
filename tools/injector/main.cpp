#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs=std::filesystem;
struct Handle {
    HANDLE h=nullptr;
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
std::uintptr_t Module(DWORD pid,const std::wstring& name,bool fullPath=false) {
    Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};
    MODULEENTRY32W m{};m.dwSize=sizeof(m);
    if(Module32FirstW(snap.h,&m)) do {
        if(_wcsicmp(fullPath?m.szExePath:m.szModule,name.c_str())==0)
            return reinterpret_cast<std::uintptr_t>(m.modBaseAddr);
    } while(Module32NextW(snap.h,&m));
    return 0;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3){std::wcerr<<L"Usage: preyvr_injector <Prey PID> <full DLL path>\n";return 2;}
    wchar_t* end=nullptr;const unsigned long parsed=wcstoul(argv[1],&end,10);
    if(!parsed || !end || *end){return 2;}
    const DWORD pid=static_cast<DWORD>(parsed);
    const auto dll=fs::absolute(argv[2]).lexically_normal();
    if(!fs::is_regular_file(dll)){std::wcerr<<L"DLL missing\n";return 2;}
    Handle process{OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|
        PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid)};
    if(!process.h){std::wcerr<<L"OpenProcess failed "<<GetLastError()<<L'\n';return 3;}
    wchar_t exe[32768]{};DWORD size=32768;
    if(!QueryFullProcessImageNameW(process.h,0,exe,&size) ||
        _wcsicmp(fs::path(exe).filename().c_str(),L"Prey.exe")){
        std::wcerr<<L"Target is not Prey.exe\n";return 3;
    }
    USHORT machine=0,native=0;
    if(!IsWow64Process2(process.h,&machine,&native) || machine!=IMAGE_FILE_MACHINE_UNKNOWN ||
        native!=IMAGE_FILE_MACHINE_AMD64 || !Module(pid,L"PreyDll.dll")){
        std::wcerr<<L"Target is not an initialized x64 Prey process\n";return 3;
    }
    if(Module(pid,L"PreyVR.dll")){std::wcerr<<L"PreyVR is already loaded; restart to change builds\n";return 4;}
    // Resolve the actual module owning the export (KernelBase on current Windows),
    // then add its RVA to THAT module's remote base. Never assume equal ASLR bases.
    const auto local=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    MEMORY_BASIC_INFORMATION memory{};
    if(!local || !VirtualQuery(reinterpret_cast<void*>(local),&memory,sizeof(memory)))return 5;
    wchar_t ownerPath[MAX_PATH]{};
    GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),ownerPath,MAX_PATH);
    const auto owner=Module(pid,fs::path(ownerPath).filename().wstring());
    if(!owner){std::wcerr<<L"Loader module is absent in target\n";return 5;}
    const auto remoteFn=owner+reinterpret_cast<std::uintptr_t>(local)-
                              reinterpret_cast<std::uintptr_t>(memory.AllocationBase);
    const auto text=dll.wstring(); const SIZE_T bytes=(text.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(process.h,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    SIZE_T written=0;
    if(!remote || !WriteProcessMemory(process.h,remote,text.c_str(),bytes,&written) || written!=bytes){
        if(remote)VirtualFreeEx(process.h,remote,0,MEM_RELEASE);return 6;
    }
    Handle thread{CreateRemoteThread(process.h,nullptr,0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteFn),remote,0,nullptr)};
    if(!thread.h){VirtualFreeEx(process.h,remote,0,MEM_RELEASE);return 7;}
    if(WaitForSingleObject(thread.h,30000)!=WAIT_OBJECT_0){
        // Remote thread may still read its argument: do not free underneath it.
        std::wcerr<<L"Loader timed out; no retry in this process\n";return 8;
    }
    VirtualFreeEx(process.h,remote,0,MEM_RELEASE);
    if(!Module(pid,text,true)){std::wcerr<<L"DLL did not load from the requested path\n";return 9;}
    std::wcout<<L"Loaded "<<text<<L" into Prey PID "<<pid<<L'\n';return 0;
}
