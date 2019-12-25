/*
 *	Process synchronisation
 *
 * Copyright 1996, 1997, 1998 Marcus Meissner
 * Copyright 1997, 1999 Alexandre Julliard
 * Copyright 1999, 2000 Juergen Schmied
 * Copyright 2003 Eric Pouech
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
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
#endif

#if defined(_MSC_VER)
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

static inline atomic_t interlocked_dec_if_nonzero(atomic_t *dest )
{
    atomic_t val, tmp;
    for (val = *dest;; val = tmp)
    {
        if (!val || (tmp = interlocked_cmpxchg( dest, val - 1, val )) == val)
            break;
    }
    return val;
}

#define SRWLOCK_MASK_IN_EXCLUSIVE     0x80000000
#define SRWLOCK_MASK_EXCLUSIVE_QUEUE  0x7fff0000
#define SRWLOCK_MASK_SHARED_QUEUE     0x0000ffff
#define SRWLOCK_RES_EXCLUSIVE         0x00010000
#define SRWLOCK_RES_SHARED            0x00000001

#ifdef WORDS_BIGENDIAN
#define srwlock_key_exclusive(lock)   (&lock->Ptr)
#define srwlock_key_shared(lock)      ((void *)((char *)&lock->Ptr + 2))
#else
#define srwlock_key_exclusive(lock)   ((void *)((char *)&lock->Ptr + 2))
#define srwlock_key_shared(lock)      (&lock->Ptr)
#endif

static inline PLARGE_INTEGER get_nt_timeout( PLARGE_INTEGER pTime, DWORD timeout )
{
    if (timeout == INFINITE) return NULL;
    pTime->QuadPart = (ULONGLONG)timeout * -10000;
    return pTime;
}

static inline void srwlock_check_invalid( unsigned int val )
{
    /* Throw exception if it's impossible to acquire/release this lock. */
    if ((val & SRWLOCK_MASK_EXCLUSIVE_QUEUE) == SRWLOCK_MASK_EXCLUSIVE_QUEUE ||
            (val & SRWLOCK_MASK_SHARED_QUEUE) == SRWLOCK_MASK_SHARED_QUEUE)
        gRtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
}

static inline unsigned int srwlock_lock_exclusive( unsigned int *dest, int incr )
{
    unsigned int val, tmp;
    /* Atomically modifies the value of *dest by adding incr. If the shared
     * queue is empty and there are threads waiting for exclusive access, then
     * sets the mark SRWLOCK_MASK_IN_EXCLUSIVE to signal other threads that
     * they are allowed again to use the shared queue counter. */
    for (val = *dest;; val = tmp)
    {
        tmp = val + incr;
        srwlock_check_invalid( tmp );
        if ((tmp & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(tmp & SRWLOCK_MASK_SHARED_QUEUE))
            tmp |= SRWLOCK_MASK_IN_EXCLUSIVE;
        if ((tmp = interlocked_cmpxchg( (atomic_t *)dest, tmp, val )) == val)
            break;
    }
    return val;
}

static inline unsigned int srwlock_unlock_exclusive( unsigned int *dest, int incr )
{
    unsigned int val, tmp;
    /* Atomically modifies the value of *dest by adding incr. If the queue of
     * threads waiting for exclusive access is empty, then remove the
     * SRWLOCK_MASK_IN_EXCLUSIVE flag (only the shared queue counter will
     * remain). */
    for (val = *dest;; val = tmp)
    {
        tmp = val + incr;
        srwlock_check_invalid( tmp );
        if (!(tmp & SRWLOCK_MASK_EXCLUSIVE_QUEUE))
            tmp &= SRWLOCK_MASK_SHARED_QUEUE;
        if ((tmp = interlocked_cmpxchg( (atomic_t *)dest, tmp, val )) == val)
            break;
    }
    return val;
}

static inline void srwlock_leave_exclusive( RTL_SRWLOCK *lock, unsigned int val )
{
    /* Used when a thread leaves an exclusive section. If there are other
     * exclusive access threads they are processed first, followed by
     * the shared waiters. */
    if (val & SRWLOCK_MASK_EXCLUSIVE_QUEUE)
        gNtReleaseKeyedEvent( keyed_event, srwlock_key_exclusive(lock), FALSE, NULL );
    else
    {
        val &= SRWLOCK_MASK_SHARED_QUEUE; /* remove SRWLOCK_MASK_IN_EXCLUSIVE */
        while (val--)
            gNtReleaseKeyedEvent( keyed_event, srwlock_key_shared(lock), FALSE, NULL );
    }
}

static inline void srwlock_leave_shared( RTL_SRWLOCK *lock, unsigned int val )
{
    /* Wake up one exclusive thread as soon as the last shared access thread
     * has left. */
    if ((val & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(val & SRWLOCK_MASK_SHARED_QUEUE))
        gNtReleaseKeyedEvent( keyed_event, srwlock_key_exclusive(lock), FALSE, NULL );
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


/***********************************************************************
 *              RtlInitializeSRWLock (NTDLL.@)
 *
 * NOTES
 *  Please note that SRWLocks do not keep track of the owner of a lock.
 *  It doesn't make any difference which thread for example unlocks an
 *  SRWLock (see corresponding tests). This implementation uses two
 *  keyed events (one for the exclusive waiters and one for the shared
 *  waiters) and is limited to 2^15-1 waiting threads.
 */
void WINAPI RtlInitializeSRWLock( RTL_SRWLOCK *lock )
{
    lock->Ptr = NULL;
}

/***********************************************************************
 *              RtlAcquireSRWLockExclusive (NTDLL.@)
 *
 * NOTES
 *  Unlike RtlAcquireResourceExclusive this function doesn't allow
 *  nested calls from the same thread. "Upgrading" a shared access lock
 *  to an exclusive access lock also doesn't seem to be supported.
 */
void WINAPI RtlAcquireSRWLockExclusive( RTL_SRWLOCK *lock )
{
    if (srwlock_lock_exclusive( (unsigned int *)&lock->Ptr, SRWLOCK_RES_EXCLUSIVE ))
        gNtWaitForKeyedEvent( keyed_event, srwlock_key_exclusive(lock), FALSE, NULL );
}

/***********************************************************************
 *              RtlAcquireSRWLockShared (NTDLL.@)
 *
 * NOTES
 *   Do not call this function recursively - it will only succeed when
 *   there are no threads waiting for an exclusive lock!
 */
void WINAPI RtlAcquireSRWLockShared( RTL_SRWLOCK *lock )
{
    unsigned int val, tmp;
    /* Acquires a shared lock. If it's currently not possible to add elements to
     * the shared queue, then request exclusive access instead. */
    for (val = *(unsigned int *)&lock->Ptr;; val = tmp)
    {
        if ((val & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(val & SRWLOCK_MASK_IN_EXCLUSIVE))
            tmp = val + SRWLOCK_RES_EXCLUSIVE;
        else
            tmp = val + SRWLOCK_RES_SHARED;
        if ((tmp = interlocked_cmpxchg( (atomic_t *)&lock->Ptr, tmp, val )) == val)
            break;
    }

    /* Drop exclusive access again and instead requeue for shared access. */
    if ((val & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(val & SRWLOCK_MASK_IN_EXCLUSIVE))
    {
        gNtWaitForKeyedEvent( keyed_event, srwlock_key_exclusive(lock), FALSE, NULL );
        val = srwlock_unlock_exclusive( (unsigned int *)&lock->Ptr, (SRWLOCK_RES_SHARED
                                        - SRWLOCK_RES_EXCLUSIVE) ) - SRWLOCK_RES_EXCLUSIVE;
        srwlock_leave_exclusive( lock, val );
    }

    if (val & SRWLOCK_MASK_EXCLUSIVE_QUEUE)
        gNtWaitForKeyedEvent( keyed_event, srwlock_key_shared(lock), FALSE, NULL );
}

/***********************************************************************
 *              RtlReleaseSRWLockExclusive (NTDLL.@)
 */
void WINAPI RtlReleaseSRWLockExclusive( RTL_SRWLOCK *lock )
{
    srwlock_leave_exclusive( lock, srwlock_unlock_exclusive( (unsigned int *)&lock->Ptr,
                             - SRWLOCK_RES_EXCLUSIVE ) - SRWLOCK_RES_EXCLUSIVE );
}

/***********************************************************************
 *              RtlReleaseSRWLockShared (NTDLL.@)
 */
void WINAPI RtlReleaseSRWLockShared( RTL_SRWLOCK *lock )
{
    srwlock_leave_shared( lock, srwlock_lock_exclusive( (unsigned int *)&lock->Ptr,
                          - SRWLOCK_RES_SHARED ) - SRWLOCK_RES_SHARED );
}

/***********************************************************************
 *              RtlTryAcquireSRWLockExclusive (NTDLL.@)
 *
 * NOTES
 *  Similar to AcquireSRWLockExclusive recusive calls are not allowed
 *  and will fail with return value FALSE.
 */
BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive( RTL_SRWLOCK *lock )
{
    return interlocked_cmpxchg( (atomic_t *)&lock->Ptr, SRWLOCK_MASK_IN_EXCLUSIVE |
                                SRWLOCK_RES_EXCLUSIVE, 0 ) == 0;
}

/***********************************************************************
 *              RtlTryAcquireSRWLockShared (NTDLL.@)
 */
BOOLEAN WINAPI RtlTryAcquireSRWLockShared( RTL_SRWLOCK *lock )
{
    unsigned int val, tmp;
    for (val = *(unsigned int *)&lock->Ptr;; val = tmp)
    {
        if (val & SRWLOCK_MASK_EXCLUSIVE_QUEUE)
            return FALSE;
        if ((tmp = interlocked_cmpxchg( (atomic_t *)&lock->Ptr, val + SRWLOCK_RES_SHARED, val )) == val)
            break;
    }
    return TRUE;
}

/***********************************************************************
 *           RtlInitializeConditionVariable   (NTDLL.@)
 *
 * Initializes the condition variable with NULL.
 *
 * PARAMS
 *  variable [O] condition variable
 *
 * RETURNS
 *  Nothing.
 */
void WINAPI RtlInitializeConditionVariable( RTL_CONDITION_VARIABLE *variable )
{
    variable->Ptr = NULL;
}

/***********************************************************************
 *           RtlWakeConditionVariable   (NTDLL.@)
 *
 * Wakes up one thread waiting on the condition variable.
 *
 * PARAMS
 *  variable [I/O] condition variable to wake up.
 *
 * RETURNS
 *  Nothing.
 *
 * NOTES
 *  The calling thread does not have to own any lock in order to call
 *  this function.
 */
void WINAPI RtlWakeConditionVariable( RTL_CONDITION_VARIABLE *variable )
{
    if (interlocked_dec_if_nonzero( (atomic_t *)&variable->Ptr ))
        gNtReleaseKeyedEvent( keyed_event, &variable->Ptr, FALSE, NULL );
}

/***********************************************************************
 *           RtlWakeAllConditionVariable   (NTDLL.@)
 *
 * See WakeConditionVariable, wakes up all waiting threads.
 */
void WINAPI RtlWakeAllConditionVariable( RTL_CONDITION_VARIABLE *variable )
{
    int val = interlocked_xchg( (atomic_t *)&variable->Ptr, 0 );
    while (val-- > 0)
        gNtReleaseKeyedEvent( keyed_event, &variable->Ptr, FALSE, NULL );
}

/***********************************************************************
 *           RtlSleepConditionVariableCS   (NTDLL.@)
 *
 * Atomically releases the critical section and suspends the thread,
 * waiting for a Wake(All)ConditionVariable event. Afterwards it enters
 * the critical section again and returns.
 *
 * PARAMS
 *  variable  [I/O] condition variable
 *  crit      [I/O] critical section to leave temporarily
 *  timeout   [I]   timeout
 *
 * RETURNS
 *  see NtWaitForKeyedEvent for all possible return values.
 */
NTSTATUS
WINAPI RtlSleepConditionVariableCS( RTL_CONDITION_VARIABLE *variable, RTL_CRITICAL_SECTION *crit,
                                    const LARGE_INTEGER *timeout )
{
    NTSTATUS status;
    interlocked_xchg_add( (atomic_t *)&variable->Ptr, 1 );
    gRtlLeaveCriticalSection( crit );

    status = gNtWaitForKeyedEvent( keyed_event, &variable->Ptr, FALSE, timeout );
    if (status != STATUS_SUCCESS)
    {
        if (!interlocked_dec_if_nonzero( (atomic_t *)&variable->Ptr ))
            status = gNtWaitForKeyedEvent( keyed_event, &variable->Ptr, FALSE, NULL );
    }

    gRtlEnterCriticalSection( crit );
    return status;
}

/***********************************************************************
 *           RtlSleepConditionVariableSRW   (NTDLL.@)
 *
 * Atomically releases the SRWLock and suspends the thread,
 * waiting for a Wake(All)ConditionVariable event. Afterwards it enters
 * the SRWLock again with the same access rights and returns.
 *
 * PARAMS
 *  variable  [I/O] condition variable
 *  lock      [I/O] SRWLock to leave temporarily
 *  timeout   [I]   timeout
 *  flags     [I]   type of the current lock (exclusive / shared)
 *
 * RETURNS
 *  see NtWaitForKeyedEvent for all possible return values.
 *
 * NOTES
 *  the behaviour is undefined if the thread doesn't own the lock.
 */

NTSTATUS WINAPI RtlSleepConditionVariableSRW( RTL_CONDITION_VARIABLE *variable, RTL_SRWLOCK *lock,
                                              const LARGE_INTEGER *timeout, ULONG flags )
{
    NTSTATUS status;
    interlocked_xchg_add( (atomic_t *)&variable->Ptr, 1 );

    if (flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED)
        RtlReleaseSRWLockShared( lock );
    else
        RtlReleaseSRWLockExclusive( lock );

    status = gNtWaitForKeyedEvent( keyed_event, &variable->Ptr, FALSE, timeout );
    if (status != STATUS_SUCCESS)
    {
        if (!interlocked_dec_if_nonzero( (atomic_t *)&variable->Ptr ))
            status = gNtWaitForKeyedEvent( keyed_event, &variable->Ptr, FALSE, NULL );
    }

    if (flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED)
        RtlAcquireSRWLockShared( lock );
    else
        RtlAcquireSRWLockExclusive( lock );
    return status;
}

BOOL WINAPI MySleepConditionVariableSRW( RTL_CONDITION_VARIABLE *variable, RTL_SRWLOCK *lock, DWORD timeout, ULONG flags )
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
