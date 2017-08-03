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
  rustû��ʹ��InitializeConditionVariable������࿴�ú���ֻ�ǰ�RTL_CONDITION_VARIABLE.Ptr��0����rust���new�߼�һ��
*/
/* condvar��ʵ����һ�����ƣ�����û��init��free����ĺ��� */
// rust��ʵ�ֵģ���ϧ����merge�� https://github.com/rust-lang/rust/pull/27036/files  �������ûͨ������
// ����reactOS���ף�����ȱ��TryAcquireSRWLockExclusiveʵ�֣���һ��ʱ�������
// https://github.com/reactos/reactos/tree/master/reactos/dll/win32/kernel32_vista
// https://github.com/reactos/reactos/blob/master/reactos/dll/win32/ntdll_vista
// ���⻹��wine���о�reactOS��wine�ǻ��೭��:)��wineҲ��ȱ�� TryAcquireSRWLockExclusive����wineʵ�ִ��뻹�ǲ�һ����
/* ���⻹��һ��Extended XP https://ryanvm.net/forum/viewtopic.php?t=10631 ��˵��Դ��*/
extern VOID
WINAPI
AcquireSRWLockExclusive(PSRWLOCK Lock);
extern VOID
WINAPI
AcquireSRWLockShared(PSRWLOCK Lock);
extern VOID
WINAPI
ReleaseSRWLockExclusive(PSRWLOCK Lock);
extern VOID
WINAPI
ReleaseSRWLockShared(PSRWLOCK Lock);
extern BOOL
WINAPI
SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable, PSRWLOCK Lock, DWORD Timeout, ULONG Flags);
extern VOID
WINAPI
WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);
extern VOID
WINAPI
WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);

typedef FARPROC (WINAPI *GETPROCADDRESS)(HMODULE hModule, LPCSTR  lpProcName);
GETPROCADDRESS OrgGetProcAddress = 0;
GETPROCADDRESS OrgGetProcAddress2 = 0;
static HMODULE gKernel32 = 0;
FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR  lpProcName)
{
    FARPROC ret = OrgGetProcAddress2(hModule, lpProcName);
    if(!ret && (hModule == gKernel32))
    {
        if(0 == strcmp(lpProcName, "AcquireSRWLockExclusive"))
        {
            return AcquireSRWLockExclusive;
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
                // ��1����3�������벻һ��������MH_EnableHook����4
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

