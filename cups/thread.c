//
// Threading primitives for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2009-2018 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include "thread.h"


//
// Windows threading...
//

#if _WIN32
#  include <setjmp.h>


//
// Private structures...
//

struct _cups_thread_s
{
  HANDLE	h;			// Thread handle
  void		*(*func)(void *);	// Thread start function
  void		*arg;			// Argument to pass to function
  void		*retval;		// Return value from function
  bool		canceled;		// Is the thread canceled?
  jmp_buf	jumpbuf;		// Jump buffer for error recovery
};


//
// Local functions...
//

static cups_thread_t	win32_self(void);
static void		win32_testcancel(void);
static DWORD		win32_tls(void);
static int		win32_wrapper(cups_thread_t thread);


//
// 'cupsCondBroadcast()' - Wake up waiting threads.
//

void
cupsCondBroadcast(cups_cond_t *cond)	// I - Condition variable
{
  if (cond)
    WakeAllConditionVariable(cond);
}


//
// 'cupsCondDestroy()' - Destroy a condition variable.
//

void
cupsCondDestroy(cups_cond_t *cond)	// I - Condition variable
{
  (void)cond;
}


//
// 'cupsCondInit()' - Initialize a condition variable.
//

void
cupsCondInit(cups_cond_t *cond)		// I - Condition variable
{
  if (cond)
    InitializeConditionVariable(cond);
}


//
// 'cupsCondWait()' - Wait for a condition with optional timeout.
//

void
cupsCondWait(cups_cond_t  *cond,	// I - Condition
	     cups_mutex_t *mutex,	// I - Mutex
	     double       timeout)	// I - Timeout in seconds (`0` or negative for none)
{
  win32_testcancel();

  if (cond && mutex)
  {
    if (timeout > 0.0)
      SleepConditionVariableCS(cond, mutex, (int)(1000.0 * timeout));
    else
      SleepConditionVariableCS(cond, mutex, INFINITE);
  }
}


//
// 'cupsMutexDestroy()' - Destroy a mutex.
//

void
cupsMutexDestroy(cups_mutex_t *mutex)	// I - Mutex
{
  (void)mutex;
}


//
// 'cupsMutexInit()' - Initialize a mutex.
//

void
cupsMutexInit(cups_mutex_t *mutex)	// I - Mutex
{
  if (mutex)
    InitializeCriticalSection(mutex);
}


//
// 'cupsMutexLock()' - Lock a mutex.
//

void
cupsMutexLock(cups_mutex_t *mutex)	// I - Mutex
{
  if (mutex)
    EnterCriticalSection(mutex);
}


//
// 'cupsMutexUnlock()' - Unlock a mutex.
//

void
cupsMutexUnlock(cups_mutex_t *mutex)	// I - Mutex
{
  if (mutex)
    LeaveCriticalSection(mutex);
}


//
// 'cupsRWDestroy()' - Destroy a reader/writer lock.
//

void
cupsRWDestroy(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  (void)rwlock;
}


//
// 'cupsRWInit()' - Initialize a reader/writer lock.
//

void
cupsRWInit(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  if (rwlock)
    InitializeSRWLock(rwlock);
}


//
// 'cupsRWLockRead()' - Acquire a reader/writer lock for reading.
//

void
cupsRWLockRead(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  if (rwlock)
    AcquireSRWLockShared(rwlock);
}


//
// 'cupsRWLockWrite()' - Acquire a reader/writer lock for writing.
//

void
cupsRWLockWrite(cups_rwlock_t *rwlock)// I - Reader/writer lock
{
  if (rwlock)
    AcquireSRWLockExclusive(rwlock);
}


//
// 'cupsRWUnlock()' - Release a reader/writer lock.
//

void
cupsRWUnlock(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  if (rwlock)
  {
    void	*val = *(void **)rwlock;// Lock value

    if (val == (void *)1)
      ReleaseSRWLockExclusive(rwlock);
    else
      ReleaseSRWLockShared(rwlock);
  }
}


//
// 'cupsThreadCancel()' - Cancel (kill) a thread.
//

void
cupsThreadCancel(cups_thread_t thread)// I - Thread ID
{
  if (thread)
    thread->canceled = true;
}


//
// 'cupsThreadCreate()' - Create a thread.
//

cups_thread_t				// O - Thread ID or `CUPS_THREAD_INVALID` on failure
cupsThreadCreate(
    cups_thread_func_t func,		// I - Entry point
    void               *arg)		// I - Entry point context
{
  cups_thread_t	thread;			// Thread data


  if (!func)
    return (CUPS_THREAD_INVALID);

  if ((thread = (cups_thread_t)calloc(1, sizeof(struct _cups_thread_s))) == NULL)
    return (CUPS_THREAD_INVALID);

  thread->func = func;
  thread->arg  = arg;
  thread->h    = (HANDLE)_beginthreadex(NULL, 0, (LPTHREAD_START_ROUTINE)win32_wrapper, thread, 0, NULL);

  if (thread->h == 0 || thread->h == (HANDLE)-1)
  {
    free(thread);
    return (CUPS_THREAD_INVALID);
  }

  return (thread);
}


//
// 'cupsThreadDetach()' - Tell the OS that the thread is running independently.
//

void
cupsThreadDetach(cups_thread_t thread)// I - Thread ID
{
  if (thread)
  {
    CloseHandle(thread->h);
    thread->h = 0;
  }
}


//
// 'cupsThreadWait()' - Wait for a thread to exit.
//

void *					// O - Return value
cupsThreadWait(cups_thread_t thread)	// I - Thread ID
{
  void	*retval;			// Return value


  if (!thread)
    return (NULL);

  win32_testcancel();

  if (thread->h)
  {
    WaitForSingleObject(thread->h, INFINITE);
    CloseHandle(thread->h);
  }

  retval = thread->retval;

  free(thread);

  return (retval);
}


//
// 'win32_self()' - Return the current thread.
//

static cups_thread_t			// O - Thread
win32_self(void)
{
  cups_thread_t	thread;			// Thread


  if ((thread = TlsGetValue(win32_tls())) == NULL)
  {
    // Main thread, so create the info we need...
    if ((thread = (cups_thread_t)calloc(1, sizeof(struct _cups_thread_s))) != NULL)
    {
      thread->h = GetCurrentThread();
      TlsSetValue(win32_tls(), thread);

      if (setjmp(thread->jumpbuf))
      {
        if (!thread->h)
          free(thread);

        _endthreadex(0);
      }
    }
  }

  return (thread);
}


//
// 'win32_testcancel()' - Mark a safe cancellation point.
//

static void
win32_testcancel(void)
{
  cups_thread_t	thread;			// Current thread


  // Go to the thread's exit handler if we've been canceled...
  if ((thread = win32_self()) != NULL && thread->canceled)
    longjmp(thread->jumpbuf, 1);
}


//
// 'win32_tls()' - Get the thread local storage key.
//

static DWORD				// O - Key
win32_tls(void)
{
  static DWORD	tls = 0;		// Thread local storage key
  static CRITICAL_SECTION tls_mutex = { (void*)-1, -1, 0, 0, 0, 0 };
					// Lock for thread local storage access


  EnterCriticalSection(&tls_mutex);
  if (!tls)
  {
    if ((tls = TlsAlloc()) == TLS_OUT_OF_INDEXES)
      abort();
  }
  LeaveCriticalSection(&tls_mutex);

  return (tls);
}


//
// 'win32_wrapper()' - Wrapper function for a POSIX thread.
//

static int				// O - Exit status
win32_wrapper(cups_thread_t thread)	// I - Thread
{
  TlsSetValue(win32_tls(), thread);

  if (!setjmp(thread->jumpbuf))
  {
    // Call function in thread...
    thread->retval = (thread->func)(thread->arg);
  }

  // Clean up...
  while (thread->h == (HANDLE)-1)
  {
    // win32_create hasn't finished initializing the handle...
    YieldProcessor();
    _ReadWriteBarrier();
  }

  // Free if detached...
  if (!thread->h)
    free(thread);

  return (0);
}


#else
//
// POSIX threading...
//

//
// 'cupsCondBroadcast()' - Wake up waiting threads.
//

void
cupsCondBroadcast(cups_cond_t *cond)	// I - Condition
{
  pthread_cond_broadcast(cond);
}


//
// 'cupsCondDestroy()' - Destroy a condition variable.
//

void
cupsCondDestroy(cups_cond_t *cond)	// I - Condition
{
  pthread_cond_destroy(cond);
}


//
// 'cupsCondInit()' - Initialize a condition variable.
//

void
cupsCondInit(cups_cond_t *cond)		// I - Condition
{
  pthread_cond_init(cond, NULL);
}


//
// 'cupsCondWait()' - Wait for a condition with optional timeout.
//

void
cupsCondWait(cups_cond_t  *cond,	// I - Condition
	     cups_mutex_t *mutex,	// I - Mutex
	     double       timeout)	// I - Timeout in seconds (`0` or negative for none)
{
  if (timeout > 0.0)
  {
    struct timespec abstime;		// Timeout

    clock_gettime(CLOCK_REALTIME, &abstime);

    abstime.tv_sec  += (long)timeout;
    abstime.tv_nsec += (long)(1000000000 * (timeout - (long)timeout));

    while (abstime.tv_nsec >= 1000000000)
    {
      abstime.tv_nsec -= 1000000000;
      abstime.tv_sec ++;
    };

    (void)pthread_cond_timedwait(cond, mutex, &abstime);
  }
  else
    (void)pthread_cond_wait(cond, mutex);
}


//
// 'cupsMutexDestroy()' - Destroy a mutex.
//

void
cupsMutexDestroy(cups_mutex_t *mutex)	// I - Mutex
{
  pthread_mutex_destroy(mutex);
}


//
// 'cupsMutexInit()' - Initialize a mutex.
//

void
cupsMutexInit(cups_mutex_t *mutex)	// I - Mutex
{
  pthread_mutex_init(mutex, NULL);
}


//
// 'cupsMutexLock()' - Lock a mutex.
//

void
cupsMutexLock(cups_mutex_t *mutex)	// I - Mutex
{
  pthread_mutex_lock(mutex);
}


//
// 'cupsMutexUnlock()' - Unlock a mutex.
//

void
cupsMutexUnlock(cups_mutex_t *mutex)	// I - Mutex
{
  pthread_mutex_unlock(mutex);
}


//
// 'cupsRWDestroy()' - Destroy a reader/writer lock.
//

void
cupsRWDestroy(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  pthread_rwlock_destroy(rwlock);
}


//
// 'cupsRWInit()' - Initialize a reader/writer lock.
//

void
cupsRWInit(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  pthread_rwlock_init(rwlock, NULL);
}


//
// 'cupsRWLockRead()' - Acquire a reader/writer lock for reading.
//

void
cupsRWLockRead(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  pthread_rwlock_rdlock(rwlock);
}


//
// 'cupsRWLockWrite()' - Acquire a reader/writer lock for writing.
//

void
cupsRWLockWrite(cups_rwlock_t *rwlock)// I - Reader/writer lock
{
  pthread_rwlock_wrlock(rwlock);
}


//
// 'cupsRWUnlock()' - Release a reader/writer lock.
//

void
cupsRWUnlock(cups_rwlock_t *rwlock)	// I - Reader/writer lock
{
  pthread_rwlock_unlock(rwlock);
}


//
// 'cupsThreadCancel()' - Cancel (kill) a thread.
//

void
cupsThreadCancel(cups_thread_t thread)// I - Thread ID
{
  pthread_cancel(thread);
}


//
// 'cupsThreadCreate()' - Create a thread.
//

cups_thread_t				// O - Thread ID or `CUPS_THREAD_INVALID` on failure
cupsThreadCreate(
    cups_thread_func_t func,		// I - Entry point
    void               *arg)		// I - Entry point context
{
  pthread_t thread;			// Thread


  if (pthread_create(&thread, NULL, (void *(*)(void *))func, arg))
    return (CUPS_THREAD_INVALID);
  else
    return (thread);
}


//
// 'cupsThreadDetach()' - Tell the OS that the thread is running independently.
//

void
cupsThreadDetach(cups_thread_t thread)// I - Thread ID
{
  pthread_detach(thread);
}


//
// 'cupsThreadWait()' - Wait for a thread to exit.
//

void *					// O - Return value
cupsThreadWait(cups_thread_t thread)	// I - Thread ID
{
  void	*ret;				// Return value


  if (pthread_join(thread, &ret))
    return (NULL);
  else
    return (ret);
}
#endif // _WIN32
