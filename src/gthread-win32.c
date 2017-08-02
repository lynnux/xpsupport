// from https://github.com/GNOME/glib/blob/master/glib/gthread-win32.c

#include <windows.h>
typedef PVOID gpointer;


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

typedef struct
{
    volatile GThreadXpWaiter  *first;
    volatile GThreadXpWaiter **last_ptr;
} GThreadXpCONDITION_VARIABLE;

static BOOL __stdcall
g_thread_xp_SleepConditionVariableSRW (gpointer cond,
                                       gpointer mutex,
                                       DWORD    timeout,
                                       ULONG    flags)
{
    GThreadXpCONDITION_VARIABLE *cv = g_thread_xp_get_condition_variable (cond);
    GThreadXpWaiter *waiter = g_thread_xp_waiter_get ();
    DWORD status;

    waiter->next = NULL;

    EnterCriticalSection (&g_thread_xp_lock);
    waiter->my_owner = cv->last_ptr;
    *cv->last_ptr = waiter;
    cv->last_ptr = &waiter->next;
    LeaveCriticalSection (&g_thread_xp_lock);

    g_mutex_unlock (mutex);
    status = WaitForSingleObject (waiter->event, timeout);

    if (status != WAIT_TIMEOUT && status != WAIT_OBJECT_0)
        g_thread_abort (GetLastError (), "WaitForSingleObject");
    g_mutex_lock (mutex);

    if (status == WAIT_TIMEOUT)
    {
        EnterCriticalSection (&g_thread_xp_lock);
        if (waiter->my_owner)
        {
            if (waiter->next)
                waiter->next->my_owner = waiter->my_owner;
            else
                cv->last_ptr = waiter->my_owner;
            *waiter->my_owner = waiter->next;
            waiter->my_owner = NULL;
        }
        LeaveCriticalSection (&g_thread_xp_lock);
    }

    return status == WAIT_OBJECT_0;
}
