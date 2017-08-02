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

// rust有实现的，可惜不给merge啊 https://github.com/rust-lang/rust/pull/27036/files
// conditon varible https://github.com/GNOME/glib/blob/master/glib/gthread-win32.c
//

/* {{{1 SRWLock and CONDITION_VARIABLE emulation (for Windows XP) */

static CRITICAL_SECTION g_thread_xp_lock;
static DWORD            g_thread_xp_waiter_tls;

/* {{{2 GThreadWaiter utility class for CONDITION_VARIABLE emulation */
typedef struct _GThreadXpWaiter GThreadXpWaiter;
struct _GThreadXpWaiter
{
    HANDLE                     event;
    volatile GThreadXpWaiter  *next;
    volatile GThreadXpWaiter **my_owner;
};

typedef FARPROC (WINAPI *GETPROCADDRESS)(HMODULE hModule, LPCSTR  lpProcName);
GETPROCADDRESS OrgGetProcAddress = 0;
FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR  lpProcName)
{
    return OrgGetProcAddress(hModule, lpProcName);
}



extern "C" void dllmain()
{
    static bool inited = false;
    if(!inited)
    {
        inited = true;
        MH_Initialize();

        HMODULE mod = GetModuleHandleW(L"kernel32.dll");
        if(mod)
        {
            OrgGetProcAddress = (GETPROCADDRESS)GetProcAddress(mod, "GetProcAddress");
            if(OrgGetProcAddress)
            {
                if(MH_OK == MH_CreateHook((PVOID)OrgGetProcAddress, (PVOID)HookGetProcAddress, (PVOID*)&OrgGetProcAddress))
                {
                    MH_EnableHook((PVOID)OrgGetProcAddress);
                }
            }
        }
    }
}

#if defined(_MSC_VER)
extern "C" BOOL WINAPI _DllMainCRTStartup(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
#else
extern "C" BOOL WINAPI DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
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

