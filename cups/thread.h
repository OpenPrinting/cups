//
// Threading definitions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2009-2017 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_THREAD_H_
#  define _CUPS_THREAD_H_
#  include "base.h"
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Windows threading...
//

#  if _WIN32
#    include <winsock2.h>
#    include <process.h>
#    include <windows.h>
typedef void *(__stdcall *cups_thread_func_t)(void *arg);
					// Thread function
typedef struct _cups_thread_s *cups_thread_t;
					// Thread identifier
typedef CONDITION_VARIABLE cups_cond_t;	// Condition variable
typedef CRITICAL_SECTION cups_mutex_t;	// Mutual exclusion lock
typedef SRWLOCK cups_rwlock_t;		// Reader/writer lock
typedef DWORD	cups_thread_key_t;	// Thread data key
#    define CUPS_COND_INITIALIZER { 0 }
#    define CUPS_MUTEX_INITIALIZER { (void*)-1, -1, 0, 0, 0, 0 }
#    define CUPS_RWLOCK_INITIALIZER { 0 }
#    define CUPS_THREADKEY_INITIALIZER 0
#    define cupsThreadGetData(k) TlsGetValue(k)
#    define cupsThreadSetData(k,p) TlsSetValue(k,p)
#  else


//
// POSIX threading...
//

#    include <pthread.h>
typedef void *(*cups_thread_func_t)(void *arg);
					// Thread function
typedef pthread_t cups_thread_t;	// Thread identifier
typedef pthread_cond_t cups_cond_t;	// Condition variable
typedef pthread_mutex_t cups_mutex_t;	// Mutual exclusion lock
typedef pthread_rwlock_t cups_rwlock_t;	// Reader/writer lock
typedef pthread_key_t	cups_thread_key_t;
					// Thread data key
#    define CUPS_COND_INITIALIZER PTHREAD_COND_INITIALIZER
#    define CUPS_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#    define CUPS_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER
#    define CUPS_THREADKEY_INITIALIZER 0
#    define cupsThreadGetData(k) pthread_getspecific(k)
#    define cupsThreadSetData(k,p) pthread_setspecific(k,p)
#  endif // _WIN32
#  define CUPS_THREAD_INVALID (cups_thread_t)0


//
// Functions...
//

extern void	cupsCondBroadcast(cups_cond_t *cond) _CUPS_PUBLIC;
extern void	cupsCondDestroy(cups_cond_t *cond) _CUPS_PUBLIC;
extern void	cupsCondInit(cups_cond_t *cond) _CUPS_PUBLIC;
extern void	cupsCondWait(cups_cond_t *cond, cups_mutex_t *mutex, double timeout) _CUPS_PUBLIC;

extern void	cupsMutexDestroy(cups_mutex_t *mutex) _CUPS_PUBLIC;
extern void	cupsMutexInit(cups_mutex_t *mutex) _CUPS_PUBLIC;
extern void	cupsMutexLock(cups_mutex_t *mutex) _CUPS_PUBLIC;
extern void	cupsMutexUnlock(cups_mutex_t *mutex) _CUPS_PUBLIC;

extern void	cupsRWDestroy(cups_rwlock_t *rwlock) _CUPS_PUBLIC;
extern void	cupsRWInit(cups_rwlock_t *rwlock) _CUPS_PUBLIC;
extern void	cupsRWLockRead(cups_rwlock_t *rwlock) _CUPS_PUBLIC;
extern void	cupsRWLockWrite(cups_rwlock_t *rwlock) _CUPS_PUBLIC;
extern void	cupsRWUnlock(cups_rwlock_t *rwlock) _CUPS_PUBLIC;

extern void	cupsThreadCancel(cups_thread_t thread) _CUPS_PUBLIC;
extern cups_thread_t cupsThreadCreate(cups_thread_func_t func, void *arg) _CUPS_PUBLIC;
extern void     cupsThreadDetach(cups_thread_t thread) _CUPS_PUBLIC;
extern void	*cupsThreadWait(cups_thread_t thread) _CUPS_PUBLIC;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_THREAD_H_
