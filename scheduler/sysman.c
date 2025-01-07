/*
 * System management functions for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright @ 2007-2018 by Apple Inc.
 * Copyright @ 2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */


/*
 * Include necessary headers...
 */

#include "cupsd.h"
#ifdef __APPLE__
#  include <IOKit/pwr_mgt/IOPMLib.h>
#endif /* __APPLE__ */


/*
 * The system management functions cover disk and power management which
 * are primarily used for portable computers.
 *
 * Disk management involves delaying the write of certain configuration
 * and state files to minimize the number of times the disk has to spin
 * up or flash to be written to.
 *
 * Power management support is currently only implemented on macOS, but
 * essentially we use four functions to let the OS know when it is OK to
 * put the system to sleep, typically when we are not in the middle of
 * printing a job.  And on macOS we can also "sleep print" - basically the
 * system only wakes up long enough to service network requests and process
 * print jobs.
 */


/*
 * 'cupsdCleanDirty()' - Write dirty config and state files.
 */

void
cupsdCleanDirty(void)
{
  if (DirtyFiles & CUPSD_DIRTY_PRINTERS)
    cupsdSaveAllPrinters();

  if (DirtyFiles & CUPSD_DIRTY_CLASSES)
    cupsdSaveAllClasses();

  if (DirtyFiles & CUPSD_DIRTY_PRINTCAP)
    cupsdWritePrintcap();

  if (DirtyFiles & CUPSD_DIRTY_JOBS)
  {
    cupsd_job_t	*job;			/* Current job */

    cupsdSaveAllJobs();

    for (job = (cupsd_job_t *)cupsArrayFirst(Jobs);
         job;
	 job = (cupsd_job_t *)cupsArrayNext(Jobs))
      if (job->dirty)
        cupsdSaveJob(job);
  }

  if (DirtyFiles & CUPSD_DIRTY_SUBSCRIPTIONS)
    cupsdSaveAllSubscriptions();

  DirtyFiles     = CUPSD_DIRTY_NONE;
  DirtyCleanTime = 0;

  cupsdSetBusyState(0);
}


/*
 * 'cupsdMarkDirty()' - Mark config or state files as needing a write.
 */

void
cupsdMarkDirty(int what)		/* I - What file(s) are dirty? */
{
  cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdMarkDirty(%c%c%c%c%c)",
		  (what & CUPSD_DIRTY_PRINTERS) ? 'P' : '-',
		  (what & CUPSD_DIRTY_CLASSES) ? 'C' : '-',
		  (what & CUPSD_DIRTY_PRINTCAP) ? 'p' : '-',
		  (what & CUPSD_DIRTY_JOBS) ? 'J' : '-',
		  (what & CUPSD_DIRTY_SUBSCRIPTIONS) ? 'S' : '-');

  if (what == CUPSD_DIRTY_PRINTCAP && !Printcap)
    return;

  DirtyFiles |= what;

  if (!DirtyCleanTime)
    DirtyCleanTime = time(NULL) + DirtyCleanInterval;

  cupsdSetBusyState(0);
}


/*
 * 'cupsdSetBusyState()' - Let the system know when we are busy doing something.
 */

void
cupsdSetBusyState(int working)          /* I - Doing significant work? */
{
  int			i;		/* Looping var */
  cupsd_job_t		*job;		/* Current job */
  cupsd_printer_t	*p;		/* Current printer */
  int			newbusy;	/* New busy state */
  static int		busy = 0;	/* Current busy state */
  static const char * const busy_text[] =
  {					/* Text for busy states */
    "Not busy",
    "Dirty files",
    "Printing jobs",
    "Printing jobs and dirty files",
    "Active clients",
    "Active clients and dirty files",
    "Active clients and printing jobs",
    "Active clients, printing jobs, and dirty files"
  };
#ifdef __APPLE__
  static IOPMAssertionID keep_awake = 0;/* Keep the system awake while printing */
#endif /* __APPLE__ */


 /*
  * Figure out how busy we are...
  */

  newbusy = (DirtyCleanTime ? 1 : 0) |
	    ((working || cupsArrayCount(ActiveClients) > 0) ? 4 : 0);

  for (job = (cupsd_job_t *)cupsArrayFirst(PrintingJobs);
       job;
       job = (cupsd_job_t *)cupsArrayNext(PrintingJobs))
  {
    if ((p = job->printer) != NULL)
    {
      for (i = 0; i < p->num_reasons; i ++)
	if (!strcmp(p->reasons[i], "connecting-to-device"))
	  break;

      if (!p->num_reasons || i >= p->num_reasons)
	break;
    }
  }

  if (job)
    newbusy |= 2;

  cupsdLogMessage(CUPSD_LOG_DEBUG,
                  "cupsdSetBusyState: newbusy=\"%s\", busy=\"%s\"",
                  busy_text[newbusy], busy_text[busy]);

 /*
  * Manage state changes...
  */

  if (newbusy != busy)
    busy = newbusy;

#ifdef __APPLE__
  if (cupsArrayCount(PrintingJobs) > 0 && !keep_awake)
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG, "Asserting NetworkClientActive.");

    IOPMAssertionCreateWithName(kIOPMAssertNetworkClientActive,
				kIOPMAssertionLevelOn,
				CFSTR("org.cups.cupsd"), &keep_awake);
  }
  else if (cupsArrayCount(PrintingJobs) == 0 && keep_awake)
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG, "Releasing power assertion.");
    IOPMAssertionRelease(keep_awake);
    keep_awake = 0;
  }
#endif /* __APPLE__ */
}


#ifdef __APPLE__
/*
 * This is the Apple-specific system event code.  It works by creating
 * a worker thread that waits for events from the OS and relays them
 * to the main thread via a traditional pipe.
 */

/*
 * Include MacOS-specific headers...
 */

#  include <notify.h>
#  include <IOKit/IOKitLib.h>
#  include <IOKit/IOMessage.h>
#  include <IOKit/ps/IOPowerSources.h>
#  include <IOKit/pwr_mgt/IOPMLib.h>
#  include <SystemConfiguration/SystemConfiguration.h>
#  include <pthread.h>


/*
 * Constants...
 */

#  define SYSEVENT_CANSLEEP	0x1	/* Decide whether to allow sleep or not */
#  define SYSEVENT_WILLSLEEP	0x2	/* Computer will go to sleep */
#  define SYSEVENT_WOKE		0x4	/* Computer woke from sleep */
#  define SYSEVENT_NETCHANGED	0x8	/* Network changed */
#  define SYSEVENT_NAMECHANGED	0x10	/* Computer name changed */


/*
 * Structures...
 */

typedef struct cupsd_sysevent_s		/*** System event data ****/
{
  unsigned char	event;			/* Event bit field */
  io_connect_t	powerKernelPort;	/* Power context data */
  long		powerNotificationID;	/* Power event data */
} cupsd_sysevent_t;


typedef struct cupsd_thread_data_s	/*** Thread context data  ****/
{
  cupsd_sysevent_t	sysevent;	/* System event */
  CFRunLoopTimerRef	timerRef;	/* Timer to delay some change *
					 * notifications              */
} cupsd_thread_data_t;


/*
 * Local globals...
 */

static pthread_t	SysEventThread = NULL;
					/* Thread to host a runloop */
static pthread_mutex_t	SysEventThreadMutex = { 0 };
					/* Coordinates access to shared gloabals */
static pthread_cond_t	SysEventThreadCond = { 0 };
					/* Thread initialization complete condition */
static CFRunLoopRef	SysEventRunloop = NULL;
					/* The runloop. Access must be protected! */
static CFStringRef	ComputerNameKey = NULL,
					/* Computer name key */
			BTMMKey = NULL,	/* Back to My Mac key */
			NetworkGlobalKeyIPv4 = NULL,
					/* Network global IPv4 key */
			NetworkGlobalKeyIPv6 = NULL,
					/* Network global IPv6 key */
			NetworkGlobalKeyDNS = NULL,
					/* Network global DNS key */
			HostNamesKey = NULL,
					/* Host name key */
			NetworkInterfaceKeyIPv4 = NULL,
					/* Network interface key */
			NetworkInterfaceKeyIPv6 = NULL;
					/* Network interface key */
static cupsd_sysevent_t	LastSysEvent;	/* Last system event (for delayed sleep) */
static int		NameChanged = 0;/* Did we get a 'name changed' event during sleep? */
static int		PSToken = 0;	/* Power source notifications */


/*
 * Local functions...
 */

static void	*sysEventThreadEntry(void);
static void	sysEventPowerNotifier(void *context, io_service_t service,
		                      natural_t messageType,
				      void *messageArgument);
static void	sysEventConfigurationNotifier(SCDynamicStoreRef store,
		                              CFArrayRef changedKeys,
					      void *context);
static void	sysEventTimerNotifier(CFRunLoopTimerRef timer, void *context);
static void	sysUpdate(void);
static void	sysUpdateNames(void);


/*
 * 'cupsdAllowSleep()' - Tell the OS it is now OK to sleep.
 */

void
cupsdAllowSleep(void)
{
  cupsdCleanDirty();

  cupsdLogMessage(CUPSD_LOG_DEBUG, "Allowing system sleep.");
  IOAllowPowerChange(LastSysEvent.powerKernelPort,
		     LastSysEvent.powerNotificationID);
}


/*
 * 'cupsdStartSystemMonitor()' - Start monitoring for system change.
 */

void
cupsdStartSystemMonitor(void)
{
  int	flags;				/* fcntl flags on pipe */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdStartSystemMonitor()");

  if (cupsdOpenPipe(SysEventPipes))
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "System event monitor pipe() failed - %s!",
                    strerror(errno));
    return;
  }

  cupsdAddSelect(SysEventPipes[0], (cupsd_selfunc_t)sysUpdate, NULL, NULL);

 /*
  * Set non-blocking mode on the descriptor we will be receiving notification
  * events on.
  */

  flags = fcntl(SysEventPipes[0], F_GETFL, 0);
  fcntl(SysEventPipes[0], F_SETFL, flags | O_NONBLOCK);

 /*
  * Start the thread that runs the runloop...
  */

  pthread_mutex_init(&SysEventThreadMutex, NULL);
  pthread_cond_init(&SysEventThreadCond, NULL);
  pthread_create(&SysEventThread, NULL, (void *(*)(void *))sysEventThreadEntry, NULL);

 /*
  * Monitor for power source changes via dispatch queue...
  */

  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdStartSystemMonitor: IOPSGetTimeRemainingEstimate=%f", IOPSGetTimeRemainingEstimate());
  ACPower = IOPSGetTimeRemainingEstimate() == kIOPSTimeRemainingUnlimited;
  notify_register_dispatch(kIOPSNotifyPowerSource, &PSToken, dispatch_get_main_queue(), ^(int t) { (void)t;
      ACPower = IOPSGetTimeRemainingEstimate() == kIOPSTimeRemainingUnlimited; });
}


/*
 * 'cupsdStopSystemMonitor()' - Stop monitoring for system change.
 */

void
cupsdStopSystemMonitor(void)
{
  CFRunLoopRef	rl;			/* The event handler runloop */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdStopSystemMonitor()");

  if (SysEventThread)
  {
   /*
    * Make sure the thread has completed it's initialization and
    * stored it's runloop reference in the shared global.
    */

    pthread_mutex_lock(&SysEventThreadMutex);

    if (!SysEventRunloop)
      pthread_cond_wait(&SysEventThreadCond, &SysEventThreadMutex);

    rl              = SysEventRunloop;
    SysEventRunloop = NULL;

    pthread_mutex_unlock(&SysEventThreadMutex);

    if (rl)
      CFRunLoopStop(rl);

    pthread_join(SysEventThread, NULL);
    pthread_mutex_destroy(&SysEventThreadMutex);
    pthread_cond_destroy(&SysEventThreadCond);
  }

  if (SysEventPipes[0] >= 0)
  {
    cupsdRemoveSelect(SysEventPipes[0]);
    cupsdClosePipe(SysEventPipes);
  }

  if (PSToken != 0)
  {
    notify_cancel(PSToken);
    PSToken = 0;
  }
}


/*
 * 'sysEventThreadEntry()' - A thread to receive power and computer name
 *                           change notifications.
 */

static void *				/* O - Return status/value */
sysEventThreadEntry(void)
{
  io_object_t		powerNotifierObj;
					/* Power notifier object */
  IONotificationPortRef powerNotifierPort;
					/* Power notifier port */
  SCDynamicStoreRef	store    = NULL;/* System Config dynamic store */
  CFRunLoopSourceRef	powerRLS = NULL,/* Power runloop source */
			storeRLS = NULL;/* System Config runloop source */
  CFStringRef		key[6],		/* System Config keys */
			pattern[2];	/* System Config patterns */
  CFArrayRef		keys = NULL,	/* System Config key array*/
			patterns = NULL;/* System Config pattern array */
  SCDynamicStoreContext	storeContext;	/* Dynamic store context */
  CFRunLoopTimerContext timerContext;	/* Timer context */
  cupsd_thread_data_t	threadData;	/* Thread context data for the *
					 * runloop notifiers           */


 /*
  * Register for power state change notifications
  */

  memset(&threadData, 0, sizeof(threadData));

  threadData.sysevent.powerKernelPort =
      IORegisterForSystemPower(&threadData, &powerNotifierPort,
                               sysEventPowerNotifier, &powerNotifierObj);

  if (threadData.sysevent.powerKernelPort)
  {
    powerRLS = IONotificationPortGetRunLoopSource(powerNotifierPort);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), powerRLS, kCFRunLoopDefaultMode);
  }

 /*
  * Register for system configuration change notifications
  */

  memset(&storeContext, 0, sizeof(storeContext));
  storeContext.info = &threadData;

  store = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("cupsd"),
                               sysEventConfigurationNotifier, &storeContext);

  if (!ComputerNameKey)
    ComputerNameKey = SCDynamicStoreKeyCreateComputerName(kCFAllocatorDefault);

  if (!BTMMKey)
    BTMMKey = SCDynamicStoreKeyCreate(kCFAllocatorDefault,
                                      CFSTR("Setup:/Network/BackToMyMac"));

  if (!NetworkGlobalKeyIPv4)
    NetworkGlobalKeyIPv4 =
        SCDynamicStoreKeyCreateNetworkGlobalEntity(kCFAllocatorDefault,
                                                   kSCDynamicStoreDomainState,
						   kSCEntNetIPv4);

  if (!NetworkGlobalKeyIPv6)
    NetworkGlobalKeyIPv6 =
        SCDynamicStoreKeyCreateNetworkGlobalEntity(kCFAllocatorDefault,
                                                   kSCDynamicStoreDomainState,
						   kSCEntNetIPv6);

  if (!NetworkGlobalKeyDNS)
    NetworkGlobalKeyDNS =
	SCDynamicStoreKeyCreateNetworkGlobalEntity(kCFAllocatorDefault,
						   kSCDynamicStoreDomainState,
						   kSCEntNetDNS);

  if (!HostNamesKey)
    HostNamesKey = SCDynamicStoreKeyCreateHostNames(kCFAllocatorDefault);

  if (!NetworkInterfaceKeyIPv4)
    NetworkInterfaceKeyIPv4 =
        SCDynamicStoreKeyCreateNetworkInterfaceEntity(kCFAllocatorDefault,
	                                              kSCDynamicStoreDomainState,
						      kSCCompAnyRegex,
						      kSCEntNetIPv4);

  if (!NetworkInterfaceKeyIPv6)
    NetworkInterfaceKeyIPv6 =
        SCDynamicStoreKeyCreateNetworkInterfaceEntity(kCFAllocatorDefault,
	                                              kSCDynamicStoreDomainState,
						      kSCCompAnyRegex,
						      kSCEntNetIPv6);

  if (store && ComputerNameKey && HostNamesKey &&
      NetworkGlobalKeyIPv4 && NetworkGlobalKeyIPv6 && NetworkGlobalKeyDNS &&
      NetworkInterfaceKeyIPv4 && NetworkInterfaceKeyIPv6)
  {
    key[0]     = ComputerNameKey;
    key[1]     = BTMMKey;
    key[2]     = NetworkGlobalKeyIPv4;
    key[3]     = NetworkGlobalKeyIPv6;
    key[4]     = NetworkGlobalKeyDNS;
    key[5]     = HostNamesKey;

    pattern[0] = NetworkInterfaceKeyIPv4;
    pattern[1] = NetworkInterfaceKeyIPv6;

    keys     = CFArrayCreate(kCFAllocatorDefault, (const void **)key,
			     sizeof(key) / sizeof(key[0]),
			     &kCFTypeArrayCallBacks);

    patterns = CFArrayCreate(kCFAllocatorDefault, (const void **)pattern,
                             sizeof(pattern) / sizeof(pattern[0]),
			     &kCFTypeArrayCallBacks);

    if (keys && patterns &&
        SCDynamicStoreSetNotificationKeys(store, keys, patterns))
    {
      if ((storeRLS = SCDynamicStoreCreateRunLoopSource(kCFAllocatorDefault,
                                                        store, 0)) != NULL)
      {
	CFRunLoopAddSource(CFRunLoopGetCurrent(), storeRLS,
	                   kCFRunLoopDefaultMode);
      }
    }
  }

  if (keys)
    CFRelease(keys);

  if (patterns)
    CFRelease(patterns);

 /*
  * Set up a timer to delay the wake change notifications.
  *
  * The initial time is set a decade or so into the future, we'll adjust
  * this later.
  */

  memset(&timerContext, 0, sizeof(timerContext));
  timerContext.info = &threadData;

  threadData.timerRef =
      CFRunLoopTimerCreate(kCFAllocatorDefault,
                           CFAbsoluteTimeGetCurrent() + (86400L * 365L * 10L),
			   86400L * 365L * 10L, 0, 0, sysEventTimerNotifier,
			   &timerContext);
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), threadData.timerRef,
                    kCFRunLoopDefaultMode);

 /*
  * Store our runloop in a global so the main thread can use it to stop us.
  */

  pthread_mutex_lock(&SysEventThreadMutex);

  SysEventRunloop = CFRunLoopGetCurrent();

  pthread_cond_signal(&SysEventThreadCond);
  pthread_mutex_unlock(&SysEventThreadMutex);

 /*
  * Disappear into the runloop until it's stopped by the main thread.
  */

  CFRunLoopRun();

 /*
  * Clean up before exiting.
  */

  if (threadData.timerRef)
  {
    CFRunLoopRemoveTimer(CFRunLoopGetCurrent(), threadData.timerRef,
                         kCFRunLoopDefaultMode);
    CFRelease(threadData.timerRef);
  }

  if (threadData.sysevent.powerKernelPort)
  {
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), powerRLS,
                          kCFRunLoopDefaultMode);
    IODeregisterForSystemPower(&powerNotifierObj);
    IOServiceClose(threadData.sysevent.powerKernelPort);
    IONotificationPortDestroy(powerNotifierPort);
  }

  if (storeRLS)
  {
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), storeRLS,
                          kCFRunLoopDefaultMode);
    CFRunLoopSourceInvalidate(storeRLS);
    CFRelease(storeRLS);
  }

  if (store)
    CFRelease(store);

  pthread_exit(NULL);
}


/*
 * 'sysEventPowerNotifier()' - Handle power notification events.
 */

static void
sysEventPowerNotifier(
    void         *context,		/* I - Thread context data */
    io_service_t service,		/* I - Unused service info */
    natural_t    messageType,		/* I - Type of message */
    void         *messageArgument)	/* I - Message data */
{
  int			sendit = 1;	/* Send event to main thread?    *
					 * (0 = no, 1 = yes, 2 = delayed */
  cupsd_thread_data_t	*threadData;	/* Thread context data */


  threadData = (cupsd_thread_data_t *)context;

  (void)service;			/* anti-compiler-warning-code */

  switch (messageType)
  {
    case kIOMessageCanSystemPowerOff :
    case kIOMessageCanSystemSleep :
	threadData->sysevent.event |= SYSEVENT_CANSLEEP;
	break;

    case kIOMessageSystemWillRestart :
    case kIOMessageSystemWillPowerOff :
    case kIOMessageSystemWillSleep :
	threadData->sysevent.event |= SYSEVENT_WILLSLEEP;
	threadData->sysevent.event &= ~SYSEVENT_WOKE;
	break;

    case kIOMessageSystemHasPoweredOn :
       /*
	* Because powered on is followed by a net-changed event, delay
	* before sending it.
	*/

        sendit = 2;
	threadData->sysevent.event |= SYSEVENT_WOKE;
	break;

    case kIOMessageSystemWillNotPowerOff :
    case kIOMessageSystemWillNotSleep :
#  ifdef kIOMessageSystemWillPowerOn
    case kIOMessageSystemWillPowerOn :
#  endif /* kIOMessageSystemWillPowerOn */
    default:
	sendit = 0;
	break;
  }

  switch (messageType)
  {
    case kIOMessageCanSystemPowerOff :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageCanSystemPowerOff message.");
	break;
    case kIOMessageCanSystemSleep :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageCannSystemSleep message.");
	break;
    case kIOMessageSystemWillRestart :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemWillRestart message.");
	break;
    case kIOMessageSystemWillPowerOff :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemWillPowerOff message.");
	break;
    case kIOMessageSystemWillSleep :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemWillSleep message.");
	break;
    case kIOMessageSystemHasPoweredOn :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemHasPoweredOn message.");
	break;
    case kIOMessageSystemWillNotPowerOff :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemWillNotPowerOff message.");
	break;
    case kIOMessageSystemWillNotSleep :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemWillNotSleep message.");
	break;
#  ifdef kIOMessageSystemWillPowerOn
    case kIOMessageSystemWillPowerOn :
        cupsdLogMessage(CUPSD_LOG_DEBUG,
                        "Got kIOMessageSystemWillPowerOn message.");
	break;
#  endif /* kIOMessageSystemWillPowerOn */
    default:
        cupsdLogMessage(CUPSD_LOG_DEBUG, "Got unknown power message %d.",
                        (int)messageType);
	break;
  }

  if (sendit == 0)
    IOAllowPowerChange(threadData->sysevent.powerKernelPort,
                       (long)messageArgument);
  else
  {
    threadData->sysevent.powerNotificationID = (long)messageArgument;

    if (sendit == 1)
    {
     /*
      * Send the event to the main thread now.
      */

      write(SysEventPipes[1], &threadData->sysevent,
	    sizeof(threadData->sysevent));
      threadData->sysevent.event = 0;
    }
    else
    {
     /*
      * Send the event to the main thread after 1 to 2 seconds.
      */

      CFRunLoopTimerSetNextFireDate(threadData->timerRef,
                                    CFAbsoluteTimeGetCurrent() + 2);
    }
  }
}


/*
 * 'sysEventConfigurationNotifier()' - Network configuration change notification
 *                                     callback.
 */

static void
sysEventConfigurationNotifier(
    SCDynamicStoreRef store,		/* I - System data (unused) */
    CFArrayRef        changedKeys,	/* I - Changed data */
    void              *context)		/* I - Thread context data */
{
  cupsd_thread_data_t	*threadData;	/* Thread context data */


  threadData = (cupsd_thread_data_t *)context;

  (void)store;				/* anti-compiler-warning-code */

  CFRange range = CFRangeMake(0, CFArrayGetCount(changedKeys));

  if (CFArrayContainsValue(changedKeys, range, ComputerNameKey) ||
      CFArrayContainsValue(changedKeys, range, BTMMKey))
    threadData->sysevent.event |= SYSEVENT_NAMECHANGED;
  else
  {
    threadData->sysevent.event |= SYSEVENT_NETCHANGED;

   /*
    * Indicate the network interface list needs updating...
    */

    NetIFUpdate = 1;
  }

 /*
  * Because we registered for several different kinds of change notifications
  * this callback usually gets called several times in a row. We use a timer to
  * de-bounce these so we only end up generating one event for the main thread.
  */

  CFRunLoopTimerSetNextFireDate(threadData->timerRef,
  				CFAbsoluteTimeGetCurrent() + 5);
}


/*
 * 'sysEventTimerNotifier()' - Handle delayed event notifications.
 */

static void
sysEventTimerNotifier(
    CFRunLoopTimerRef timer,		/* I - Timer information */
    void              *context)		/* I - Thread context data */
{
  cupsd_thread_data_t	*threadData;	/* Thread context data */


  (void)timer;

  threadData = (cupsd_thread_data_t *)context;

 /*
  * If an event is still pending send it to the main thread.
  */

  if (threadData->sysevent.event)
  {
    write(SysEventPipes[1], &threadData->sysevent,
          sizeof(threadData->sysevent));
    threadData->sysevent.event = 0;
  }
}


/*
 * 'sysUpdate()' - Update the current system state.
 */

static void
sysUpdate(void)
{
  int			i;		/* Looping var */
  cupsd_sysevent_t	sysevent;	/* The system event */
  cupsd_printer_t	*p;		/* Printer information */


 /*
  * Drain the event pipe...
  */

  while (read(SysEventPipes[0], &sysevent, sizeof(sysevent))
             == sizeof(sysevent))
  {
    if (sysevent.event & SYSEVENT_CANSLEEP)
    {
     /*
      * If there are active printers that don't have the connecting-to-device
      * or cups-waiting-for-job-completed printer-state-reason then cancel the
      * sleep request, i.e., these reasons indicate a job that is not actively
      * doing anything...
      */

      for (p = (cupsd_printer_t *)cupsArrayFirst(Printers);
           p;
	   p = (cupsd_printer_t *)cupsArrayNext(Printers))
      {
        if (p->job)
        {
	  for (i = 0; i < p->num_reasons; i ++)
	    if (!strcmp(p->reasons[i], "connecting-to-device") ||
	        !strcmp(p->reasons[i], "cups-waiting-for-job-completed"))
	      break;

	  if (!p->num_reasons || i >= p->num_reasons)
	    break;
        }
      }

      if (p)
      {
        cupsdLogMessage(CUPSD_LOG_INFO,
	                "System sleep canceled because printer %s is active.",
	                p->name);
        IOCancelPowerChange(sysevent.powerKernelPort,
	                    sysevent.powerNotificationID);
      }
      else
      {
	cupsdLogMessage(CUPSD_LOG_DEBUG, "System wants to sleep.");
        IOAllowPowerChange(sysevent.powerKernelPort,
	                   sysevent.powerNotificationID);
      }
    }

    if (sysevent.event & SYSEVENT_WILLSLEEP)
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG, "System going to sleep.");

      Sleeping = 1;

      cupsdCleanDirty();

     /*
      * If we have no printing jobs, allow the power change immediately.
      * Otherwise set the SleepJobs time to 10 seconds in the future when
      * we'll take more drastic measures...
      */

      if (cupsArrayCount(PrintingJobs) == 0)
      {
	cupsdLogMessage(CUPSD_LOG_DEBUG, "Allowing system sleep.");
	IOAllowPowerChange(sysevent.powerKernelPort,
			   sysevent.powerNotificationID);
      }
      else
      {
       /*
	* If there are active printers that don't have the connecting-to-device
	* or cups-waiting-for-job-completed printer-state-reasons then delay the
	* sleep request, i.e., these reasons indicate a job is active...
	*/

	for (p = (cupsd_printer_t *)cupsArrayFirst(Printers);
	     p;
	     p = (cupsd_printer_t *)cupsArrayNext(Printers))
	{
	  if (p->job)
	  {
	    for (i = 0; i < p->num_reasons; i ++)
	      if (!strcmp(p->reasons[i], "connecting-to-device") ||
	          !strcmp(p->reasons[i], "cups-waiting-for-job-completed"))
		break;

	    if (!p->num_reasons || i >= p->num_reasons)
	      break;
	  }
	}

	if (p)
	{
	  cupsdLogMessage(CUPSD_LOG_INFO,
			  "System sleep delayed because printer %s is active.",
			  p->name);

	  LastSysEvent = sysevent;
	  SleepJobs    = time(NULL) + 10;
	}
	else
	{
	  cupsdLogMessage(CUPSD_LOG_DEBUG, "Allowing system sleep.");
	  IOAllowPowerChange(sysevent.powerKernelPort,
			     sysevent.powerNotificationID);
	}
      }
    }

    if (sysevent.event & SYSEVENT_WOKE)
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG, "System woke from sleep.");
      IOAllowPowerChange(sysevent.powerKernelPort,
                         sysevent.powerNotificationID);
      Sleeping = 0;

     /*
      * Make sure jobs that were queued prior to the system going to sleep don't
      * get canceled right away...
      */

      if (MaxJobTime > 0)
      {
        cupsd_job_t	*job;		/* Current job */

        for (job = (cupsd_job_t *)cupsArrayFirst(ActiveJobs);
             job;
             job = (cupsd_job_t *)cupsArrayNext(ActiveJobs))
        {
          if (job->cancel_time)
          {
            ipp_attribute_t *cancel_after = ippFindAttribute(job->attrs,
                                                             "job-cancel-after",
                                                             IPP_TAG_INTEGER);

            if (cancel_after)
              job->cancel_time = time(NULL) + ippGetInteger(cancel_after, 0);
            else
              job->cancel_time = time(NULL) + MaxJobTime;
          }
        }
      }

      if (NameChanged)
        sysUpdateNames();

      cupsdCheckJobs();
    }

    if (sysevent.event & SYSEVENT_NETCHANGED)
    {
      if (Sleeping)
        cupsdLogMessage(CUPSD_LOG_DEBUG,
	                "System network configuration changed - "
			"ignored while sleeping.");
      else
        cupsdLogMessage(CUPSD_LOG_DEBUG,
	                "System network configuration changed.");
    }

    if (sysevent.event & SYSEVENT_NAMECHANGED)
    {
      if (Sleeping)
      {
        NameChanged = 1;

        cupsdLogMessage(CUPSD_LOG_DEBUG,
	                "Computer name or BTMM domains changed - ignored while "
			"sleeping.");
      }
      else
      {
        cupsdLogMessage(CUPSD_LOG_DEBUG,
	                "Computer name or BTMM domains changed.");

        sysUpdateNames();
      }
    }
  }
}


/*
 * 'sysUpdateNames()' - Update computer and/or BTMM domains.
 */

static void
sysUpdateNames(void)
{
  cupsd_printer_t	*p;		/* Current printer */


  NameChanged = 0;

 /*
  * De-register the individual printers...
  */

  for (p = (cupsd_printer_t *)cupsArrayFirst(Printers);
       p;
       p = (cupsd_printer_t *)cupsArrayNext(Printers))
    cupsdDeregisterPrinter(p, 1);

#  ifdef HAVE_DNSSD
 /*
  * Update the computer name and BTMM domain list...
  */

  cupsdUpdateDNSSDName();
#  endif /* HAVE_DNSSD */

 /*
  * Now re-register them...
  */

  for (p = (cupsd_printer_t *)cupsArrayFirst(Printers);
       p;
       p = (cupsd_printer_t *)cupsArrayNext(Printers))
    cupsdRegisterPrinter(p);
}
#endif	/* __APPLE__ */
