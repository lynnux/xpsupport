#include <windows.h>
#include "minhook/include/MinHook.h"

/*

  pub fn SleepConditionVariableSRW(ConditionVariable: PCONDITION_VARIABLE,
  pub fn WakeConditionVariable(ConditionVariable: PCONDITION_VARIABLE)
  pub fn WakeAllConditionVariable(ConditionVariable: PCONDITION_VARIABLE)

  pub fn AcquireSRWLockExclusive(SRWLock: PSRWLOCK) -> () {
  pub fn AcquireSRWLockShared(SRWLock: PSRWLOCK) -> () {
  pub fn ReleaseSRWLockExclusive(SRWLock: PSRWLOCK) -> () {
  pub fn ReleaseSRWLockShared(SRWLock: PSRWLOCK) -> () {
  pub fn TryAcquireSRWLockExclusive(SRWLock: PSRWLOCK) -> BOOLEAN {
  pub fn TryAcquireSRWLockShared(SRWLock: PSRWLOCK) -> BOOLEAN {

  typedef struct _RTL_SRWLOCK {                            
  PVOID Ptr;                                       
  } RTL_SRWLOCK, *PRTL_SRWLOCK;                            
  #define RTL_SRWLOCK_INIT {0}                            
  typedef struct _RTL_CONDITION_VARIABLE {                    
  PVOID Ptr;                                       
  } RTL_CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE;   
  rust没有使用InitializeConditionVariable，反汇编看该函数只是把RTL_CONDITION_VARIABLE.Ptr置0，跟rust里的new逻辑一样
*/

// rust有实现的，可惜不给merge啊 https://github.com/rust-lang/rust/pull/27036/files  这个好像没通过测试
// 还是reactOS靠谱
// https://github.com/reactos/reactos/tree/master/reactos/dll/win32/kernel32_vista
// https://github.com/reactos/reactos/blob/master/reactos/dll/win32/ntdll_vista

typedef FARPROC (WINAPI *GETPROCADDRESS)(HMODULE hModule, LPCSTR  lpProcName);
GETPROCADDRESS OrgGetProcAddress = 0;
GETPROCADDRESS OrgGetProcAddress2 = 0;
static HMODULE gKernel32 = 0;
FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR  lpProcName)
{
    FARPROC ret = OrgGetProcAddress2(hModule, lpProcName);
    if(!ret && (hModule == gKernel32))
    {
        /* if(0 == strcmp()) */
        {
            
        }
    }
    return ret;
}

void dllmain()
{
    static BOOL inited = FALSE;
    if(!inited)
    {
        inited = TRUE;
        MH_Initialize();

        gKernel32 = GetModuleHandleW(L"kernel32.dll");
        if(gKernel32)
        {
            OrgGetProcAddress = GetProcAddress(gKernel32, "GetProcAddress");
            if(OrgGetProcAddress)
            {
                // 第1跟第3参数必须不一样，否则MH_EnableHook返回4
                if(MH_OK == MH_CreateHook(OrgGetProcAddress, HookGetProcAddress, &OrgGetProcAddress2))
                {
                    MH_EnableHook(OrgGetProcAddress);
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

