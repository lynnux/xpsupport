/* 
新思路：自己加载ntdll，ntdll是依赖最小的dll，主要注意：
1.只能用win7，vista缺少Try函数，win8用ZwWaitForAlertByThreadId(win8新增)代替了NtWaitForKeyedEvent
1.少量Nt调用修正，主要是Nt序号跟XP不一样

妈的ms真会折腾，也说明SRW等函数越来越快了
*/

#include <windows.h>
#include "sync.h"

#if defined(_LP64) || defined(_WIN64) || defined(__MINGW64__)
typedef long long atomic_t;
#if defined(_MSC_VER)
#define interlocked_cmpxchg _InterlockedCompareExchange64
#define interlocked_xchg _InterlockedExchange64
#define interlocked_xchg_add _InterlockedExchangeAdd64
#endif
#else
typedef long atomic_t;
#if defined(_MSC_VER)
#define interlocked_cmpxchg _InterlockedCompareExchange
#define interlocked_xchg _InterlockedExchange
#define interlocked_xchg_add _InterlockedExchangeAdd
#endif
#endif

// msvc https://github.com/tboox/tbox/blob/73dc6867df4b057844c7922c826559894b78fc39/src/tbox/platform/windows/atomic.h
// gnu https://github.com/tboox/tbox/blob/73dc6867df4b057844c7922c826559894b78fc39/src/tbox/platform/compiler/gcc/atomic.h
#if !defined(_MSC_VER)
// gnu 其实mingw好像只支持_InterlockedXXX函数
atomic_t interlocked_cmpxchg( atomic_t *dest, atomic_t xchg, atomic_t compare )
{
    return __sync_val_compare_and_swap( dest, compare, xchg ); /* 注意后两个参数的顺序，跟InterlockedCompareExchange不一样 */
}
#define interlocked_xchg __sync_lock_test_and_set
#define interlocked_xchg_add __sync_fetch_and_add

#if defined(__i386__)
static __inline__ unsigned long long __rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
#elif defined(__x86_64__)
static __inline__ unsigned long long __rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#endif // #if defined(__i386__)
#else
/* msvc */
#define inline __inline
#endif

typedef NTSTATUS (NTAPI *NTRELEASEKEYEDEVENT)(
    HANDLE Handle,
    PVOID Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER* Timeout);

typedef NTSTATUS (NTAPI *NTWAITFORKEYEDEVENT)(
    HANDLE Handle,
    PVOID Key,
    BOOLEAN Alertable,
    const LARGE_INTEGER* Timeout);

typedef PVOID POBJECT_ATTRIBUTES;
typedef NTSTATUS (NTAPI *NTCREATEKEYEDEVENT)(
    PHANDLE OutHandle,
    ACCESS_MASK AccessMask,
    POBJECT_ATTRIBUTES ObjectAttributes,
    ULONG Flags);

typedef NTSTATUS (NTAPI *NTCLOSE)(HANDLE);
typedef NTSTATUS (NTAPI *RTLENTERCRITICALSECTION) (RTL_CRITICAL_SECTION* crit);
typedef NTSTATUS (NTAPI *RTLLEAVECRITICALSECTION) (RTL_CRITICAL_SECTION* crit);
typedef VOID (NTAPI *RTLRAISESTATUS)(NTSTATUS Status);
typedef ULONG (WINAPI *RTLNTSTATUSTODOSERROR)(NTSTATUS Status);

NTCLOSE gNtClose = 0;
NTCREATEKEYEDEVENT gNtCreateKeyedEvent = 0;
NTRELEASEKEYEDEVENT gNtReleaseKeyedEvent = 0;
NTWAITFORKEYEDEVENT gNtWaitForKeyedEvent = 0;
RTLENTERCRITICALSECTION gRtlEnterCriticalSection = 0;
RTLLEAVECRITICALSECTION gRtlLeaveCriticalSection = 0;
RTLRAISESTATUS gRtlRaiseStatus = 0;
RTLNTSTATUSTODOSERROR gRtlNtStatusToDosError =0;

HANDLE keyed_event = NULL;

#if !defined(_MSC_VER)
__inline void __attribute__((__gnu_inline__, __always_inline__, __artificial__))  
_mm_pause (void)  
{  
    __asm__ __volatile__ ("pause" : : :"memory");  
}  
#endif

static inline PLARGE_INTEGER get_nt_timeout( PLARGE_INTEGER pTime, DWORD timeout )
{
    if (timeout == INFINITE) return NULL;
    pTime->QuadPart = (ULONGLONG)timeout * -10000;
    return pTime;
}

VOID RtlpInitializeKeyedEvent(VOID)
{
    ASSERT(keyed_event == NULL);
    gNtCreateKeyedEvent(&keyed_event, EVENT_ALL_ACCESS, NULL, 0);
}

VOID RtlpCloseKeyedEvent(VOID)
{
    ASSERT(keyed_event != NULL);
    gNtClose(keyed_event);
    keyed_event = NULL;
}

void WINAPI RtlAcquireSRWLockExclusive( RTL_SRWLOCK *pSRWLock )
{
   
}

void WINAPI RtlAcquireSRWLockShared( RTL_SRWLOCK *pSRWLock )
{
   
}

void WINAPI RtlReleaseSRWLockExclusive( RTL_SRWLOCK *pSRWLock )
{
   
}

void WINAPI RtlReleaseSRWLockShared( RTL_SRWLOCK *pSRWLock )
{
   
}

BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive( RTL_SRWLOCK *lock )
{
    return TRUE;
}

BOOLEAN WINAPI RtlTryAcquireSRWLockShared( RTL_SRWLOCK *pSRWLock )
{
  
    
    return TRUE;
}

void WINAPI RtlInitializeConditionVariable( RTL_CONDITION_VARIABLE *variable )
{
    variable->Ptr = NULL;
}

void WINAPI RtlWakeConditionVariable( RTL_CONDITION_VARIABLE *variable )
{

}

void WINAPI RtlWakeAllConditionVariable( RTL_CONDITION_VARIABLE *variable )
{

}

NTSTATUS WINAPI RtlSleepConditionVariableSRW( RTL_CONDITION_VARIABLE *variable, RTL_SRWLOCK *lock,
                                              const LARGE_INTEGER *timeout, ULONG flags )
{
    return STATUS_SUCCESS;
}

BOOL WINAPI MySleepConditionVariableSRW(RTL_CONDITION_VARIABLE *variable, RTL_SRWLOCK *lock, DWORD timeout, ULONG flags)
{
    NTSTATUS status;
    LARGE_INTEGER time;

    status = RtlSleepConditionVariableSRW( variable, lock, get_nt_timeout( &time, timeout ), flags );

    if (status != STATUS_SUCCESS)
    {
        SetLastError(gRtlNtStatusToDosError(status) );
        return FALSE;
    }
    return TRUE;
}

// for avoid trouble on gnu, dynamic get functions which depends ntdll
BOOL init_sync()
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if(hNtdll)
    {
        gNtClose = (NTCLOSE)GetProcAddress(hNtdll, "NtClose");
        gNtCreateKeyedEvent = (NTCREATEKEYEDEVENT)GetProcAddress(hNtdll, "NtCreateKeyedEvent");
        gNtReleaseKeyedEvent = (NTRELEASEKEYEDEVENT)GetProcAddress(hNtdll, "NtReleaseKeyedEvent");
        gNtWaitForKeyedEvent = (NTWAITFORKEYEDEVENT)GetProcAddress(hNtdll, "NtWaitForKeyedEvent");
        gRtlEnterCriticalSection =(RTLENTERCRITICALSECTION)GetProcAddress(hNtdll, "RtlEnterCriticalSection");
        gRtlLeaveCriticalSection = (RTLLEAVECRITICALSECTION)GetProcAddress(hNtdll, "RtlLeaveCriticalSection");
        gRtlRaiseStatus = (RTLRAISESTATUS)GetProcAddress(hNtdll, "RtlRaiseStatus");
        gRtlNtStatusToDosError = (RTLNTSTATUSTODOSERROR)GetProcAddress(hNtdll, "RtlNtStatusToDosError");
        
        if(gNtClose && gNtCreateKeyedEvent && gNtReleaseKeyedEvent
           && gNtWaitForKeyedEvent && gRtlEnterCriticalSection
           && gRtlLeaveCriticalSection && gRtlRaiseStatus && gRtlNtStatusToDosError)
        {
            RtlpInitializeKeyedEvent();
            return TRUE;            
        }
    }
    return FALSE;
}
