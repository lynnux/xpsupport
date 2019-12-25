#ifndef SYNC_H
#define SYNC_H

#if !defined(_MSC_VER)
#ifndef _SYNCHAPI_H_ /* avoid conflict with synchapi.h */
typedef struct _RTL_SRWLOCK { PVOID Ptr; } RTL_SRWLOCK,*PRTL_SRWLOCK;
typedef PRTL_SRWLOCK PSRWLOCK;
typedef struct _RTL_CONDITION_VARIABLE { PVOID Ptr; } RTL_CONDITION_VARIABLE,*PRTL_CONDITION_VARIABLE;
typedef PRTL_CONDITION_VARIABLE PCONDITION_VARIABLE;
typedef LONG NTSTATUS, *PNTSTATUS;
#ifndef RTL_CONDITION_VARIABLE_LOCKMODE_SHARED
#define RTL_CONDITION_VARIABLE_LOCKMODE_SHARED 0x1
#endif
#endif

#endif // !defined(_MSC_VER)

#ifndef STATUS_RESOURCE_NOT_OWNED
#define STATUS_RESOURCE_NOT_OWNED 0xC0000264
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L) 
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#ifndef ASSERT
#define ASSERT
#endif

extern BOOL init_sync();

extern VOID WINAPI RtlAcquireSRWLockExclusive(PSRWLOCK Lock);
extern VOID WINAPI RtlAcquireSRWLockShared(PSRWLOCK Lock);
extern VOID WINAPI RtlReleaseSRWLockExclusive(PSRWLOCK Lock);
extern VOID WINAPI RtlReleaseSRWLockShared(PSRWLOCK Lock);
extern BOOLEAN WINAPI RtlTryAcquireSRWLockExclusive( RTL_SRWLOCK *lock );
extern BOOLEAN WINAPI RtlTryAcquireSRWLockShared( RTL_SRWLOCK *lock );
extern BOOL WINAPI MySleepConditionVariableSRW( RTL_CONDITION_VARIABLE *variable, RTL_SRWLOCK *lock, DWORD timeout, ULONG flags );
extern VOID WINAPI RtlWakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);
extern VOID WINAPI RtlWakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);

#endif
