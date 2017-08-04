#include <windows.h>
#include "minhook/include/MinHook.h"

#if !defined(_MSC_VER)
typedef struct _RTL_SRWLOCK { PVOID Ptr; } RTL_SRWLOCK,*PRTL_SRWLOCK;
typedef PRTL_SRWLOCK PSRWLOCK;
typedef struct _RTL_CONDITION_VARIABLE { PVOID Ptr; } RTL_CONDITION_VARIABLE,*PRTL_CONDITION_VARIABLE;
typedef PRTL_CONDITION_VARIABLE PCONDITION_VARIABLE;
#endif

/*
  rust没有使用InitializeConditionVariable，反汇编看该函数只是把RTL_CONDITION_VARIABLE.Ptr置0，跟rust里的new逻辑一样
*/
/* condvar的实现有一点限制，就是没有init和free概念的函数 */
// rust有实现的，可惜不给merge啊 https://github.com/rust-lang/rust/pull/27036/files  这个好像没通过测试
// 还是reactOS靠谱，不过缺少TryAcquireSRWLockExclusive实现，等一段时间估计有
// https://github.com/reactos/reactos/tree/master/reactos/dll/win32/kernel32_vista
// https://github.com/reactos/reactos/blob/master/reactos/dll/win32/ntdll_vista
// 此外还有wine，感觉reactOS跟wine是互相抄的:)，wine也是缺少 TryAcquireSRWLockExclusive，但wine实现代码还是不一样的
/* 此外还有一个Extended XP https://ryanvm.net/forum/viewtopic.php?t=10631 据说开源了*/

// gnu版本需要把mingw-w64里的libntdll.a拷贝放入
extern BOOL init_sync();
    
extern VOID WINAPI RtlAcquireSRWLockExclusive(PSRWLOCK Lock);
extern VOID WINAPI RtlAcquireSRWLockShared(PSRWLOCK Lock);
extern VOID WINAPI RtlReleaseSRWLockExclusive(PSRWLOCK Lock);
extern VOID WINAPI RtlReleaseSRWLockShared(PSRWLOCK Lock);
extern BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive( RTL_SRWLOCK *lock );
extern BOOLEAN WINAPI RtlTryAcquireSRWLockShared( RTL_SRWLOCK *lock );

extern BOOL WINAPI RtlSleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable, PSRWLOCK Lock, DWORD Timeout, ULONG Flags);
extern VOID WINAPI RtlWakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);
extern VOID WINAPI RtlWakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);

typedef FARPROC (WINAPI *GETPROCADDRESS)(HMODULE hModule, LPCSTR  lpProcName);
GETPROCADDRESS OrgGetProcAddress = 0;
GETPROCADDRESS OrgGetProcAddress2 = 0;
static HMODULE gKernel32 = 0;

static struct {
    const char* procName;
    FARPROC address;
}proc_map[] =
{
    {"AcquireSRWLockExclusive", (FARPROC)RtlAcquireSRWLockExclusive},
    {"AcquireSRWLockShared", (FARPROC)RtlAcquireSRWLockShared},
    {"ReleaseSRWLockExclusive", (FARPROC)RtlReleaseSRWLockExclusive},
    {"ReleaseSRWLockShared", (FARPROC)RtlReleaseSRWLockShared},
    {"TryAcquireSRWLockExclusive", (FARPROC)RtlTryAcquireSRWLockExclusive},
    {"TryAcquireSRWLockShared", (FARPROC)RtlTryAcquireSRWLockShared},

    {"SleepConditionVariableSRW", (FARPROC)RtlSleepConditionVariableSRW},
    {"WakeAllConditionVariable", (FARPROC)RtlWakeAllConditionVariable},
    {"WakeConditionVariable", (FARPROC)RtlWakeConditionVariable},

    {0, 0}
};

FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR  lpProcName)
{
    FARPROC ret = OrgGetProcAddress2(hModule, lpProcName);
    if(!ret && (hModule == gKernel32))
    {
        int i =0;
        do
        {
            if(0 == strcmp(lpProcName, proc_map[i].procName))
            {
                return proc_map[i].address;
            }
        }
        while(proc_map[++i].address);
    }
    return ret;
}

void dllmain()
{
    static BOOL inited = FALSE;
    if(!inited)
    {
        inited = TRUE;
        if(init_sync())
        {
            MH_Initialize();

            gKernel32 = GetModuleHandleW(L"kernel32.dll");
            if(gKernel32)
            {
                OrgGetProcAddress = (GETPROCADDRESS)GetProcAddress(gKernel32, "GetProcAddress");
                if(OrgGetProcAddress)
                {
                    // 第1跟第3参数必须不一样，否则MH_EnableHook返回4
                    if(MH_OK == MH_CreateHook(OrgGetProcAddress, HookGetProcAddress, (LPVOID*)&OrgGetProcAddress2))
                    {
                        MH_EnableHook(OrgGetProcAddress);
                    }
                }
            }
        } 
    }
}

#if defined(_MSC_VER)
BOOL WINAPI _DllMainCRTStartup(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
#else
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
#endif
{
    switch(ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        dllmain();
        break;
        
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    default:
        break;
    }
    return TRUE;
}

