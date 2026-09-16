#include <windows.h>
#include <process.h>  // 添加此头文件以支持 _beginthreadex
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

// ==================== 用户可配置项 ====================
#define SKIN_FOLDER_NAME "Myskin"  // 自定义皮肤文件夹名称，可修改
// ====================================================

// x64
UINT8 wad_check_Buff[] = { 0xB8, 1, 0, 0, 0 };
UINT8 wad_repeat_Buff[] = { 0xEB };
std::string current_directory;        // 当前目录
std::vector<std::string> vec_path;    // wad文件路径容器
bool has_skin_files = false;          // 是否有皮肤文件

unsigned __int64 Global_wad = 0x1405E6146;          // hook地址（加载 Global.wad 的位置）
unsigned __int64 Global_call = 0x1405E0FA0;        // 加载call（实际加载函数的入口）
unsigned __int64 wad_check = 0x141184A11;         // 非官方wad文件签名过崩1
unsigned __int64 wad_repeat = 0x14118001F;        // 非官方wad文件签名过崩2

DWORD id;

// 写内存函数
void WriteMemory(void* address, void* buffer, size_t size) {
    DWORD oldProtect;
    VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(address, buffer, size);
    VirtualProtect(address, size, oldProtect, &oldProtect);
}

// 获取应用程序路径
std::string get_app_path() {
    char app_path[260] = { 0 };
    GetModuleFileNameA(0, app_path, 260);
    std::string tem = app_path;
    size_t pos = tem.rfind('\\');
    if (pos != std::string::npos) {
        return tem.substr(0, pos);
    }
    return tem;
}

// ANSI字符串转宽字符串
std::wstring str2wstr(std::string& str) {
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
    wchar_t* buffer = new wchar_t[len + 1];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), buffer, len);
    buffer[len] = '\0';
    std::wstring wstr = buffer;
    delete[] buffer;
    return wstr;
}

// 宽字符串转ANSI字符串必须保留这个游戏需要
std::string wstr2str(const std::wstring& wstr) {
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    char* buffer = new char[len + 1];
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), buffer, len, NULL, NULL);
    buffer[len] = '\0';
    std::string str = buffer;
    delete[] buffer;
    return str;
}

// 检查文件夹是否存在
bool directory_exists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

// 查找wad文件 (使用Win32 API替代std::filesystem)
int find_wad_file(std::wstring file_path, std::vector<std::string>& vec_path) {
    // 检查文件夹是否存在
    if (!directory_exists(file_path)) {
        return 0;
    }
    
    // 构建搜索路径
    std::wstring search_path = file_path + L"\\*.wad.client";
    
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    int file_count = 0;
    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring filename_w = find_data.cFileName;
            
            // 去除扩展名
            size_t dotPos = filename_w.rfind(L'.');
            if (dotPos != std::wstring::npos) {
                filename_w = filename_w.substr(0, dotPos);
            }
            
            // 转换为ANSI字符串
            std::string filename = wstr2str(filename_w);
            std::string name = std::string(SKIN_FOLDER_NAME) + "/" + filename;
            vec_path.push_back(name);
            file_count++;
        }
    } while (FindNextFileW(hFind, &find_data) != 0);
    
    FindClose(hFind);
    return file_count;
}

// 加载wad文件
void wadload(DWORD64 rcx_, DWORD64 call_, std::string filename) {
    std::string str_assembly = filename;
    char* str_buffer = new char[1024] {};
    strcpy_s(str_buffer, 1024, str_assembly.c_str());
    typedef __int64(__fastcall* fun)(unsigned __int64, const char*);
    fun load = (fun)call_;
    load(rcx_, str_buffer);
    delete[] str_buffer;
}

// 设置硬件断点
DWORD WINAPI SetBreakPointWin10(LPVOID lpMainThreadId) {
    // 如果没有皮肤文件，则不设置硬件断点
    if (!has_skin_files) {
        return 0;
    }
    
    HANDLE handle = OpenThread(THREAD_ALL_ACCESS, TRUE, (DWORD)((DWORD_PTR)lpMainThreadId));
    if (handle != 0) {
        SuspendThread(handle);
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_ALL;
        GetThreadContext(handle, &ctx);

        ctx.Dr0 = Global_wad;
        ctx.Dr7 = 0x1;

        SetThreadContext(handle, &ctx);
        ResumeThread(handle);
        CloseHandle(handle);
    }
    return 0;
}

// 异常处理函数
DWORD WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    // 如果没有皮肤文件，不处理异常
    if (!has_skin_files) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    if (pExceptionInfo->ExceptionRecord->ExceptionAddress == (PVOID)Global_wad) {
        pExceptionInfo->ContextRecord->Dr7 = 0;
        pExceptionInfo->ContextRecord->Dr0 = 0;
        if (vec_path.size() > 0) {
            for (size_t i = 0; i < vec_path.size(); i++) {
                wadload(pExceptionInfo->ContextRecord->Rcx, Global_call, vec_path[i].c_str());
            }
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// 保持线程运行
DWORD WINAPI KeepAliveThread(LPVOID) {
    while (true) {
        Sleep(1000);
    }
    return 0;
}

// 导出函数
extern "C" __declspec(dllexport) void WINAPI BreakPoint() {
    // 只有存在皮肤文件时才设置异常处理器
    if (has_skin_files) {
        AddVectoredExceptionHandler(1, (PVECTORED_EXCEPTION_HANDLER)ExceptionHandler);
        _beginthreadex(0, 0, (_beginthreadex_proc_type)SetBreakPointWin10, (void*)((DWORD_PTR)id), 0, 0);
    }

    while (true) {
        Sleep(1000);
    }
}

// DLL入口点
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        id = GetCurrentThreadId();

        // 应用崩溃补丁（始终执行）
        WriteProcessMemory((HANDLE)-1, (LPVOID)wad_check, wad_check_Buff, sizeof(wad_check_Buff), 0);
        WriteProcessMemory((HANDLE)-1, (LPVOID)wad_repeat, wad_repeat_Buff, sizeof(wad_repeat_Buff), 0);

        // 检查皮肤文件夹并加载皮肤文件
        current_directory = get_app_path() + "\\DATA\\FINAL\\" + std::string(SKIN_FOLDER_NAME);
        int file_count = find_wad_file(str2wstr(current_directory), vec_path);
        
        if (file_count > 0 && vec_path.size() > 0) {
            has_skin_files = true;
            // 有皮肤文件时才启动皮肤加载线程
            _beginthreadex(0, 0, (_beginthreadex_proc_type)BreakPoint, 0, 0, 0);
        } else {
            has_skin_files = false;
            // 没有皮肤文件，只打补丁，不加载皮肤
            // 直接进入无限循环保持DLL运行
            _beginthreadex(0, 0, (_beginthreadex_proc_type)KeepAliveThread, 0, 0, 0);
        }
    }
    return TRUE;
}