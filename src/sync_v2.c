/* 根据测试发现RtlSleepConditionVariableSRW有问题，但实在无法解决
   网上发现有篇国人写的文章http://blog.csdn.net/yichigo/article/details/36898561，这直接就是逆向还原代码啊，牛B，不过还是需要自己去道向Try开头函数
   srw的实现跟内核的pushlock是差不多的，关键词RtlBackoff https://github.com/mic101/windows/blob/master/WRK-v1.2/base/ntos/ex/pushlock.c
   连flag的值都一样
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

#define SRWLockSpinCount 1024  
#define Busy_Lock       1   // 已经有人获取了锁  
#define Wait_Lock       2   // 有人等待锁  
#define Release_Lock    4   // 说明已经有人释放一次锁  
#define Mixed_Lock      8   // 共享锁、独占锁并存
#define EXTRACT_ADDR(s) ((s) & (~0xf))

struct _SyncItem
{
    struct _SyncItem* back;
    struct _SyncItem* notify;
    struct _SyncItem* next;
    size_t shareCount;
    size_t flag;
};

void __stdcall RtlBackoff(unsigned int *pCount);  
void __stdcall RtlpOptimizeSRWLockList(SRWLOCK* pSRWLock, size_t st);

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

void __stdcall RtlpWakeSRWLock(SRWLOCK *pSRWLock, size_t st)  
{  
    size_t status;   
    size_t curStatus;   
    struct _SyncItem* notify;   
    struct _SyncItem* tmp1;   
    struct _SyncItem* tmp2;   
  
    status = st;  
  
    while (1)  
    {  
        if(!(status & Busy_Lock))  
        {  
            // 找出需要notify的节点  
            for(tmp1 = (struct _SyncItem*)(EXTRACT_ADDR(status)),notify = tmp1->notify; !notify; notify = tmp1->notify)  
            {  
                tmp2 = tmp1;  
                tmp1 = tmp1->back;  
                tmp1->next = tmp2;  
            }  
            ((struct _SyncItem*)(EXTRACT_ADDR(status)))->notify = notify;  
  
      
            // notify->next!=0,说明v6不是唯一一个等待的  
            // test flag bit0, notify is waiting owner-lock  
            // 因此只通知notify  
            if ( notify->next && notify->flag & 1 )//flag  
            {  
                ((struct _SyncItem *)(EXTRACT_ADDR(status)))->notify = notify->next;//notify  
                notify->next = 0;//next  
                  
                _InterlockedAnd((volatile LONG *)pSRWLock, -5);  
  
                if ( !_interlockedbittestandreset((volatile LONG *)&(notify->flag), 1u) ) //flag  
                    gNtReleaseKeyedEvent(keyed_event, (PVOID)notify, 0, 0);  
      
                return;  
            }  
      
            // notify是唯一一个等待者  
            // 或者notify是等待share锁  
            // 通知notify以及后续所有节点(如果有的话)  
            curStatus = _InterlockedCompareExchange((volatile LONG *)pSRWLock, 0, status);  
  
            if ( curStatus == status )  
            {// change status successfully.  
                tmp2 = notify;  
  
                do  
                {  
                    notify = notify->next;//通知之后，v6中的地址可能在其他线程释放已经无效，必须先保存next  
  
                    if ( !_interlockedbittestandreset((volatile LONG *)&(tmp2->flag), 1u) ) //flag  
                        gNtReleaseKeyedEvent(keyed_event, (PVOID)tmp2, 0, 0);  
                      
                    tmp2 = notify;  
                }while ( notify );  
  
                return;  
            }  
      
            // status was changed by other thread earlier than me  
            // change status failed  
            ((struct _SyncItem*)(EXTRACT_ADDR(status)))->notify = notify; //notify  
      
            status = curStatus;  
        }  
        else  
        {  
            curStatus = _InterlockedCompareExchange((volatile LONG *)pSRWLock, status - 4, status);  
            if ( curStatus == status )  
                return;  
  
            status = curStatus;  
        }  
    }  
}  
void __stdcall RtlBackoff(unsigned int *pCount)  
{  
    unsigned int nBackCount;  
  
    nBackCount = *pCount;  
    if ( nBackCount )  
    {  
        if ( nBackCount < 0x1FFF )  
            nBackCount *= 2;  
    }  
    else  
    {  
        // __readfsdword(24) --> teb  
        // teb+48           --> peb  
        // peb+100          --> NumberOfProcessors  
        if ( *(DWORD *)(*(DWORD *)(__readfsdword(24) + 48) + 100) == 1 ) // 获取cpu个数(核数)  
            return;  
  
        // ================for ia64================  
        // NtCurrentTeb()  
        // teb+48h  --> tid(64bits)  
        // teb+60h  --> peb(64bits)  
        // peb+b8h  --> NumberOfProcessors(32bits)  
  
        nBackCount = 64;  
    }  
  
    nBackCount = ((nBackCount - 1) & __rdtsc()) + nBackCount;  
  
    for ( unsigned int i = 0; i < nBackCount; i++ )  
    {  
        _mm_pause();  
    }  
  
    return;  
}

void __stdcall RtlpOptimizeSRWLockList(SRWLOCK* pSRWLock, size_t st)  
{  
    size_t status;   
    struct _SyncItem* tmp1;   
    struct _SyncItem* tmp2;   
    size_t curStatus;   
  
    status = st;  
  
    while ( 1 )  
    {     
        if ( status & Busy_Lock )  
        {  
            tmp1 = (struct _SyncItem*)(EXTRACT_ADDR(status));  
            if ( tmp1 )  
            {  
                while ( !tmp1->notify )  
                {  
                    tmp2 = tmp1;  
                    tmp1 = (struct _SyncItem *)tmp1->back; // *v3 ->back pointer of list-entry  
                    tmp1->next = tmp2; // *v3+8 ->next pointer of list-entry  
                }  
  
                ((struct _SyncItem*)(EXTRACT_ADDR(status)))->notify = tmp1->notify;   
            }  
  
            curStatus = InterlockedCompareExchange((volatile LONG *)pSRWLock, status - 4, status); // v2-4, set v2 not released  
            if ( curStatus == status )  
                break;  
  
            status = curStatus;  
        }  
        else  
        {  
            RtlpWakeSRWLock(pSRWLock, status);  
            break;  
        }  
    }  
  
    return;  
}  

VOID
RtlpInitializeKeyedEvent(VOID)
{
    ASSERT(keyed_event == NULL);
    gNtCreateKeyedEvent(&keyed_event, EVENT_ALL_ACCESS, NULL, 0);
}

VOID
RtlpCloseKeyedEvent(VOID)
{
    ASSERT(keyed_event != NULL);
    gNtClose(keyed_event);
    keyed_event = NULL;
}

void WINAPI RtlAcquireSRWLockExclusive( RTL_SRWLOCK *pSRWLock )
{
    __declspec( align( 16 ) ) struct _SyncItem syn = {0};
    size_t newStatus;   
    size_t curStatus;   
    size_t lastStatus;   
    unsigned int nBackOff = 0;   
    char bOptimize;  
  
    if ( _interlockedbittestandset((volatile LONG *)pSRWLock, 0) )  
    {  
        lastStatus = (size_t)pSRWLock->Ptr;  
  
        while (1)  
        {  
            if ( lastStatus & Busy_Lock )// locked  
            {  
                //          if ( RtlpWaitCouldDeadlock() )  
                //              ZwTerminateProcess((HANDLE)0xFFFFFFFF, -1073741749);  
                syn.flag = 3;  
                syn.next = 0;  
                bOptimize = 0;  
  
                if ( lastStatus & Wait_Lock )// someone is waiting the lock earlier than me.  
                {  
                    syn.notify = NULL;  
                    syn.shareCount = 0;  
                    syn.back = (struct _SyncItem *)(EXTRACT_ADDR(lastStatus));  
                    newStatus = (size_t)&syn | lastStatus & 8 | 7;// (8==1000b，继承混合等待的状态标志) (7==0111b)  
                      
                    if ( !(lastStatus & Release_Lock) )// v15 & 0100b, lock is not released now  
                        bOptimize = 1;  
                }  
                else// i am the first one to wait the lock.(另外，全部是share-lock的情况下，也不存在有人等待的情况)  
                {  
                    syn.notify = &syn;// i must be the next one who want to wait the lock         
                    syn.shareCount = (size_t)lastStatus >> 4;  
                    if ( syn.shareCount > 1 )  
                    {// share locked by other thread  
                        newStatus = (size_t)&syn | 0xB;  
                    }  
                    else  
                    {// i am the first one want owner-lock  
                        newStatus = (size_t)&syn | 3;  
                    }  
                }  
      
                //if value in lock has not been changed by other thread,   
                // change it with my value!  
                curStatus = _InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus, lastStatus);  
      
                if ( curStatus != lastStatus )// not changed by me  
                {  
                    RtlBackoff(&nBackOff);  
                    lastStatus = (size_t)pSRWLock->Ptr;  
                    continue;  
                }  
      
                if ( bOptimize )  
                    RtlpOptimizeSRWLockList(pSRWLock, newStatus);  
      
                for ( int i = SRWLockSpinCount; i>0; --i )  
                {  
                    // flag(bit1) can be reset by release-lock operation in other thread  
                    if ( !(syn.flag & 2) )  
                        break;  
                    _mm_pause();  
                }  
  
                // if flag(bit1) reset by Release operation,  
                // no need to wait event anymore  
                if ( _interlockedbittestandreset((volatile LONG *)&syn.flag, 1u) )  
                    gNtWaitForKeyedEvent(keyed_event, &syn, 0, 0);  
      
                lastStatus = curStatus;  
            }  
            else  
            {  
                // try to get lock  
                if ( _InterlockedCompareExchange((volatile LONG *)pSRWLock, lastStatus + 1, lastStatus) == lastStatus )  
                    return;// get lock successfully.  
      
                // status of the lock was changed by other thread  
                // get lock failed  
                RtlBackoff(&nBackOff);  
                lastStatus = (size_t)pSRWLock->Ptr;  
            }  
        }  
    }  
  
    return;  
}

void WINAPI RtlAcquireSRWLockShared( RTL_SRWLOCK *pSRWLock )
{
    __declspec( align( 16 ) ) struct _SyncItem syn = {0};  
  
    size_t newStatus;   
    size_t curStatus;   
    size_t lastStatus;   
    unsigned int nBackOff = 0;   
    char bOptimize;  
  
    lastStatus = _InterlockedCompareExchange((volatile LONG *)pSRWLock, 17, 0);  
      
    if ( lastStatus )// someone is looking at the lock  
    {  
        while ( 1 )  
        {  
            // get_share_lock 只有在有人独占锁的情况才会等待  
            // x & 1，有人获取了锁  
            // x & Wait_Lock != 0，有人在等待锁释放（必定存在独占锁）  
            // (x & 0xFFFFFFF0) == 0，有人独占锁，但是可能还没有人等待  
            if ( lastStatus & Busy_Lock && ((lastStatus & Wait_Lock) != 0 || !(EXTRACT_ADDR(lastStatus))) )  
            {  
//              if ( RtlpWaitCouldDeadlock() )  
//                  ZwTerminateProcess((HANDLE)0xFFFFFFFF, -1073741749);  
                syn.flag = 2;  
                syn.shareCount = 0;  
                bOptimize = 0;  
                syn.next = 0;  
  
                if ( lastStatus & Wait_Lock )// someone is waiting the lock earlier than me.  
                {  
                    syn.back = (struct _SyncItem *)(EXTRACT_ADDR(lastStatus));  
                    newStatus = (size_t)&syn | lastStatus & 9 | 6;// 9==1001 , 6==0110（因为lastStatus的bit0必为1，等价于(x & 8) | 7）  
                    syn.notify = NULL;  
                    if ( !(lastStatus & Release_Lock) )//(bit2 not set) lock is not released now.  
                        bOptimize = 1;  
                }  
                else // i am the first one to wait the lock.  
                {  
                    syn.notify = &syn;  
                    newStatus = (size_t)&syn | 3;// 3==0011b  
                }  
  
                curStatus = _InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus, lastStatus);  
                if ( curStatus == lastStatus )  
                {  
                    if ( bOptimize )  
                    {  
                        RtlpOptimizeSRWLockList(pSRWLock, newStatus);  
                    }  
  
                    for(int i = SRWLockSpinCount; i > 0; --i)  
                    {  
                        if ( !(syn.flag & 2) )// flag(bit1) can be reset by release-lock operation  
                            break;  
                        _mm_pause();  
                    }  
                    // if flag(bit1) is reset by release-lock operation  
                    // no need to wait event anymore  
                    if ( _interlockedbittestandreset((volatile LONG *)&syn.flag, 1u) )  
                        gNtWaitForKeyedEvent(keyed_event, &syn, 0, 0);  
                }  
                else  
                {  
                    RtlBackoff(&nBackOff);  
                    curStatus = (size_t)pSRWLock->Ptr;  
                }  
            }  
            else  
            {  
                if ( lastStatus & Wait_Lock )// 2 == 0010b，有人等待锁，但是没有进入if，说明bit0已经被清除  
                    newStatus = lastStatus + 1;// （有人处于过渡态，直接获取锁，不管他是哪种类型）  
                else// 当前是共享锁，没有人获取了独占锁或者等待独占锁  
                    newStatus = (lastStatus + 16) | 1;  
  
                // try to get lock  
                if ( lastStatus == _InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus, lastStatus))  
                    return;// get lock successfully  
  
                // status of the lock was changed by other thread  
                // get lock failed  
                RtlBackoff(&nBackOff);  
                curStatus = (size_t)pSRWLock->Ptr;  
            }  
  
            lastStatus = curStatus;  
        }  
    }  
  
    return;  
}

void WINAPI RtlReleaseSRWLockExclusive( RTL_SRWLOCK *pSRWLock )
{
    size_t newStatus;   
    size_t curStatus;   
    size_t lastWaiter;   
  
    lastWaiter = InterlockedExchangeAdd((volatile LONG *)pSRWLock, -1); // reset lock flag  
  
    if ( !(lastWaiter & Busy_Lock) ) // bit0 != 1  
    {  
        ASSERT("STATUS_RESOURCE_NOT_OWNED" && 0);  
    }  
  
    if ( lastWaiter & Wait_Lock &&      // some one is waiting (0010b)  
         !(lastWaiter & Release_Lock) )  // lock is not released, bit2==0(0100b)  
    {  
        newStatus = lastWaiter - 1; // reset lock flag  
        curStatus = InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus | Release_Lock, newStatus); // set released flag, set bit2 (0100b)  
  
        // lock is not changed by others, and now it is changed with my new value  
        if ( curStatus == newStatus )  
            RtlpWakeSRWLock(pSRWLock, (newStatus | Release_Lock));  
    }  
  
    return;  
}

void WINAPI RtlReleaseSRWLockShared( RTL_SRWLOCK *pSRWLock )
{
    size_t newStatus;   
    size_t curStatus;   
    size_t lastStatus;   
    struct _SyncItem* pLastNode;  
  
    lastStatus = InterlockedCompareExchange((volatile LONG *)pSRWLock, 0, 17);  
  
    if ( lastStatus != 17 ) // not single share lock, release lock failed.  
    {  
        if ( !(lastStatus & Busy_Lock) )  
        {  
            ASSERT("STATUS_RESOURCE_NOT_OWNED" && 0);  
        }  
  
        while ( 1 )  
        {  
            if ( lastStatus & Wait_Lock )  
            {  
                if ( lastStatus & Mixed_Lock ) // 两种锁混合等待  
                {  
                    pLastNode = (struct _SyncItem*)(EXTRACT_ADDR(lastStatus));  
                    while (!pLastNode->notify)  
                    {  
                        pLastNode = pLastNode->back;  
                    }  
  
                    // 既然是在释放共享锁，说明一定有人获取了共享锁  
                    // 如果有人获取了共享锁，就一定没有人获取独到占锁  
                    // 只需要把共享次数减1  
                    // 取出notify节点的共享次数变量的地址, 原子减  
                    if ( InterlockedDecrement((volatile LONG *)&(pLastNode->notify->shareCount)) > 0 )  
                    {  
                        return;  
                    }  
                }  
  
                while ( 1 )  
                {  
                    newStatus = lastStatus & (~0x9); //0xFFFFFFF6;// reset bit0 and bit3 (0110b)  
                    if ( lastStatus & Release_Lock )// test bit2 is set  
                    {  
                        curStatus = InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus, lastStatus);// reset bit0 and bit3  
                        if ( curStatus == lastStatus )  
                            return ;  
                    }  
                    else  
                    {  
                        curStatus = InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus | Release_Lock, lastStatus);// set bit2(0100b)  
                        if ( curStatus == lastStatus )  
                            return RtlpWakeSRWLock(pSRWLock, (newStatus | Release_Lock));// set bit2(0100b)  
                    }  
  
                    lastStatus = curStatus;  
                }  
  
                break;  
            }  
            else  
            {   // 只存在share lock  
                newStatus = (EXTRACT_ADDR(lastStatus)) <= 0x10 ?         // share lock count == 0  
                    0 :         // set to not locked  
                    lastStatus - 16;    // share lock count -1  
  
                curStatus = InterlockedCompareExchange((volatile LONG *)pSRWLock, newStatus, lastStatus);  
                if ( curStatus == lastStatus )  
                    break;  
  
                lastStatus = curStatus;  
            }  
        }  
    }  
  
    return;  
}

BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive( RTL_SRWLOCK *lock )
{
    return _interlockedbittestandset(lock, 0) == 0;
}

BOOLEAN WINAPI RtlTryAcquireSRWLockShared( RTL_SRWLOCK *pSRWLock )
{
    size_t newStatus;   
    size_t lastStatus;   
    unsigned int nBackOff = 0;   
    BOOLEAN result = FALSE;

    nBackOff = 0;
    lastStatus = _InterlockedCompareExchange((volatile signed __int32 *)pSRWLock, 17, 0);
    if ( lastStatus )
    {
        while ( 1 )
        {
            if ( lastStatus & Busy_Lock && ((lastStatus & Wait_Lock) || !(EXTRACT_ADDR(lastStatus))) )
                return 0;
            newStatus = lastStatus & Wait_Lock ? lastStatus + 1 : (lastStatus + 16) | 1;
            if ( _InterlockedCompareExchange((volatile signed __int32 *)pSRWLock, newStatus, lastStatus) == lastStatus )
                break;
            RtlBackoff((unsigned int *)&nBackOff);
            lastStatus = pSRWLock->Ptr;
        }
        result = 1;
    }
    else
    {
        result = 1;
    }
    return result;
    
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

BOOL WINAPI SleepConditionVariableSRW( RTL_CONDITION_VARIABLE *variable, RTL_SRWLOCK *lock, DWORD timeout, ULONG flags )
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
