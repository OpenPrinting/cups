//
// Select abstraction functions for the CUPS scheduler.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2016 by Apple Inc.
// Copyright © 2006-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
//

#include "cupsd.h"
#include <poll.h>


//
// Design Notes for Poll/Select API in CUPSD
// -----------------------------------------
//
// SUPPORTED APIS
//
//     OS              select  poll    epoll   kqueue  /dev/poll
//     --------------  ------  ------  ------  ------  ---------
//     AIX             YES     YES     NO      NO      NO
//     FreeBSD         YES     YES     NO      YES     NO
//     HP-UX           YES     YES     NO      NO      NO
//     Linux           YES     YES     YES     NO      NO
//     macOS           YES     YES     NO      YES     NO
//     NetBSD          YES     YES     NO      YES     NO
//     OpenBSD         YES     YES     NO      YES     NO
//     Solaris         YES     YES     NO      NO      YES
//     Tru64           YES     YES     NO      NO      NO
//     Windows         YES     NO      NO      NO      NO
//
//
// HIGH-LEVEL API
//
//     typedef void (*cupsd_selfunc_t)(void *data);
//
//     void cupsdStartSelect(void);
//     void cupsdStopSelect(void);
//     void cupsdAddSelect(int fd, cupsd_selfunc_t read_cb,
//                         cupsd_selfunc_t write_cb, void *data);
//     void cupsdRemoveSelect(int fd);
//     int cupsdDoSelect(int timeout);
//
//
// IMPLEMENTATION STRATEGY
//
//     0. Common Stuff
//         a. CUPS array of file descriptor to callback functions
//            and data + temporary array of removed fd's.
//         b. cupsdStartSelect() creates the arrays
//         c. cupsdStopSelect() destroys the arrays and all elements.
//         d. cupsdAddSelect() adds to the array and allocates a
//            new callback element.
//         e. cupsdRemoveSelect() removes from the active array and
//            adds to the inactive array.
//         f. _cupsd_fd_t provides a reference-counted structure for
//            tracking file descriptors that are monitored.
//         g. cupsdDoSelect() frees all inactive FDs.
//
//     1. select() O(n)
//         a. Input/Output fd_set variables, copied to working
//            copies and then used with select().
//         b. Loop through CUPS array, using FD_ISSET and calling
//            the read/write callbacks as needed.
//         c. cupsdRemoveSelect() clears fd_set bit from main and
//            working sets.
//         d. cupsdStopSelect() frees all of the memory used by the
//            CUPS array and fd_set's.
//
//     2. poll() - O(n log n)
//         a. Regular array of pollfd, sorted the same as the CUPS
//            array.
//         b. Loop through pollfd array, call the corresponding
//            read/write callbacks as needed.
//         c. cupsdAddSelect() adds first to CUPS array and flags the
//            pollfd array as invalid.
//         d. cupsdDoSelect() rebuilds pollfd array as needed, calls
//            poll(), then loops through the pollfd array looking up
//            as needed.
//         e. cupsdRemoveSelect() flags the pollfd array as invalid.
//         f. cupsdStopSelect() frees all of the memory used by the
//            CUPS array and pollfd array.
//
//     3. epoll() - O(n)
//         a. cupsdStartSelect() creates epoll file descriptor using
//            epoll_create() with the maximum fd count, and
//            allocates an events buffer for the maximum fd count.
//         b. cupsdAdd/RemoveSelect() uses epoll_ctl() to add
//            (EPOLL_CTL_ADD) or remove (EPOLL_CTL_DEL) a single
//            event using the level-triggered semantics. The event
//            user data field is a pointer to the new callback array
//            element.
//         c. cupsdDoSelect() uses epoll_wait() with the global event
//            buffer allocated in cupsdStartSelect() and then loops
//            through the events, using the user data field to find
//            the callback record.
//         d. cupsdStopSelect() closes the epoll file descriptor and
//            frees all of the memory used by the event buffer.
//
//     4. kqueue() - O(n)
//         b. cupsdStartSelect() creates kqueue file descriptor
//            using kqueue() function and allocates a global event
//            buffer.
//         c. cupsdAdd/RemoveSelect() uses EV_SET and kevent() to
//            register the changes. The event user data field is a
//            pointer to the new callback array element.
//         d. cupsdDoSelect() uses kevent() to poll for events and
//            loops through the events, using the user data field to
//            find the callback record.
//         e. cupsdStopSelect() closes the kqueue() file descriptor
//            and frees all of the memory used by the event buffer.
//
//     5. /dev/poll - O(n log n) - NOT YET IMPLEMENTED
//         a. cupsdStartSelect() opens /dev/poll and allocates an
//            array of pollfd structs; on failure to open /dev/poll,
//            revert to poll() system call.
//         b. cupsdAddSelect() writes a single pollfd struct to
//            /dev/poll with the new file descriptor and the
//            POLLIN/POLLOUT flags.
//         c. cupsdRemoveSelect() writes a single pollfd struct to
//            /dev/poll with the file descriptor and the POLLREMOVE
//            flag.
//         d. cupsdDoSelect() uses the DP_POLL ioctl to retrieve
//            events from /dev/poll and then loops through the
//            returned pollfd array, looking up the file descriptors
//            as needed.
//         e. cupsdStopSelect() closes /dev/poll and frees the
//            pollfd array.
//
// PERFORMANCE
//
//   In tests using the "make test" target with option 0 (keep cupsd
//   running) and the "testspeed" program with "-c 50 -r 1000", epoll()
//   performed 5.5% slower than select(), followed by kqueue() at 16%
//   slower than select() and poll() at 18% slower than select().  Similar
//   results were seen with twice the number of client connections.
//
//   The epoll() and kqueue() performance is likely limited by the
//   number of system calls used to add/modify/remove file
//   descriptors dynamically.  Further optimizations may be possible
//   in the area of limiting use of cupsdAddSelect() and
//   cupsdRemoveSelect(), however extreme care will be needed to avoid
//   excess CPU usage and deadlock conditions.
//
//   We may be able to improve the poll() implementation simply by
//   keeping the pollfd array sync'd with the _cupsd_fd_t array, as that
//   will eliminate the rebuilding of the array whenever there is a
//   change and eliminate the fd array lookups in the inner loop of
//   cupsdDoSelect().
//
//   Since /dev/poll will never be able to use a shadow array, it may
//   not make sense to implement support for it.  ioctl() overhead will
//   impact performance as well, so my guess would be that, for CUPS,
//   /dev/poll will yield a net performance loss.
//

//
// Local structures...
//

typedef struct _cupsd_fd_s
{
  int			fd,		// File descriptor
			use;		// Use count
  cupsd_selfunc_t	read_cb,	// Read callback
			write_cb;	// Write callback
  void			*data;		// Data pointer for callbacks
} _cupsd_fd_t;


//
// Local globals...
//

static cups_array_t	*cupsd_fds = NULL;
static int		cupsd_alloc_pollfds = 0,
			cupsd_update_pollfds = 0;
static struct pollfd	*cupsd_pollfds = NULL;


//
// Local functions...
//

static int	compare_fds(_cupsd_fd_t *a, _cupsd_fd_t *b, void *data);
static _cupsd_fd_t	*find_fd(int fd);
#define			release_fd(f) { \
			  (f)->use --; \
			  if (!(f)->use) free((f));\
			}
#define			retain_fd(f) (f)->use++


//
// 'cupsdAddSelect()' - Add a file descriptor to the list.
//

int					// O - 1 on success, 0 on error
cupsdAddSelect(int             fd,	// I - File descriptor
               cupsd_selfunc_t read_cb,	// I - Read callback
               cupsd_selfunc_t write_cb,// I - Write callback
	       void            *data)	// I - Data to pass to callback
{
  _cupsd_fd_t	*fdptr;			// File descriptor record


  // Range check input...
  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdAddSelect(fd=%d, read_cb=%p, write_cb=%p, data=%p)", fd, (void *)read_cb, (void *)write_cb, (void *)data);

  if (fd < 0)
    return (0);

  // See if this FD has already been added...
  if ((fdptr = find_fd(fd)) == NULL)
  {
    // No, add a new entry...
    if ((fdptr = calloc(1, sizeof(_cupsd_fd_t))) == NULL)
      return (0);

    fdptr->fd  = fd;
    fdptr->use = 1;

    if (!cupsArrayAdd(cupsd_fds, fdptr))
    {
      cupsdLogMessage(CUPSD_LOG_EMERG, "Unable to add fd %d to array!", fd);
      free(fdptr);
      return (0);
    }
  }

  cupsd_update_pollfds = 1;

  // Save the (new) read and write callbacks...
  fdptr->read_cb  = read_cb;
  fdptr->write_cb = write_cb;
  fdptr->data     = data;

  return (1);
}


//
// 'cupsdDoSelect()' - Do a select-like operation.
//

int					// O - Number of files or -1 on error
cupsdDoSelect(long timeout)		// I - Timeout in seconds
{
  int			nfds;		// Number of file descriptors
  _cupsd_fd_t		*fdptr;		// Current file descriptor
  struct pollfd		*pfd;		// Current pollfd structure
  int			count;		// Number of file descriptors


  count = cupsArrayCount(cupsd_fds);

  if (cupsd_update_pollfds)
  {
    // Update the cupsd_pollfds array to match the current FD array...
    cupsd_update_pollfds = 0;

    // (Re)allocate memory as needed...
    if (count > cupsd_alloc_pollfds)
    {
      int allocfds = count + 16;

      if (cupsd_pollfds)
	pfd = realloc(cupsd_pollfds, (size_t)allocfds * sizeof(struct pollfd));
      else
	pfd = malloc((size_t)allocfds * sizeof(struct pollfd));

      if (!pfd)
      {
	cupsdLogMessage(CUPSD_LOG_EMERG, "Unable to allocate %d bytes for polling.", (int)((size_t)allocfds * sizeof(struct pollfd)));

	return (-1);
      }

      cupsd_pollfds       = pfd;
      cupsd_alloc_pollfds = allocfds;
    }

    // Rebuild the array...
    for (fdptr = (_cupsd_fd_t *)cupsArrayFirst(cupsd_fds), pfd = cupsd_pollfds; fdptr; fdptr = (_cupsd_fd_t *)cupsArrayNext(cupsd_fds), pfd ++)
    {
      pfd->fd      = fdptr->fd;
      pfd->events  = 0;

      if (fdptr->read_cb)
	pfd->events |= POLLIN;

      if (fdptr->write_cb)
	pfd->events |= POLLOUT;
    }
  }

  if (timeout >= 0 && timeout < 86400)
    nfds = poll(cupsd_pollfds, (nfds_t)count, timeout * 1000);
  else
    nfds = poll(cupsd_pollfds, (nfds_t)count, -1);

  cupsdLogMessage(CUPSD_LOG_DEBUG, "poll(nfds=%d, timeout=%ld) returned %d", count, timeout < 86400 ? timeout * 1000 : -1, nfds);

  if (nfds > 0)
  {
    // Do callbacks for each file descriptor...
    for (pfd = cupsd_pollfds; count > 0; pfd ++, count --)
    {
      if (!pfd->revents)
        continue;

      if ((fdptr = find_fd(pfd->fd)) == NULL)
      {
        cupsdLogMessage(CUPSD_LOG_DEBUG, "cups_pollfds[%d] not found", pfd->fd);
        continue;
      }

      cupsdLogMessage(CUPSD_LOG_DEBUG, "cups_pollfds[%d].revents=%d", pfd->fd, pfd->revents);

      retain_fd(fdptr);

      if (fdptr->read_cb && (pfd->revents & (POLLIN | POLLERR | POLLHUP)))
        (*(fdptr->read_cb))(fdptr->data);

      if (fdptr->use > 1 && fdptr->write_cb && (pfd->revents & (POLLOUT | POLLERR | POLLHUP)))
        (*(fdptr->write_cb))(fdptr->data);

      release_fd(fdptr);
    }
  }

  // Return the number of file descriptors handled...
  return (nfds);
}


#ifdef CUPSD_IS_SELECTING
//
// 'cupsdIsSelecting()' - Determine whether we are monitoring a file
//                        descriptor.
//

int					// O - 1 if selecting, 0 otherwise
cupsdIsSelecting(int fd)		// I - File descriptor
{
  return (find_fd(fd) != NULL);
}
#endif // CUPSD_IS_SELECTING


//
// 'cupsdRemoveSelect()' - Remove a file descriptor from the list.
//

void
cupsdRemoveSelect(int fd)		// I - File descriptor
{
  _cupsd_fd_t		*fdptr;		// File descriptor record


  // Range check input...
  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdRemoveSelect(fd=%d)", fd);

  if (fd < 0)
    return;

  // Find the file descriptor...
  if ((fdptr = find_fd(fd)) == NULL)
    return;

  // Update the pollfds array...
  cupsd_update_pollfds = 1;

  // Remove the file descriptor from the active array and add to the
  // inactive array (or release, if we don't need the inactive array...)
  cupsArrayRemove(cupsd_fds, fdptr);

  release_fd(fdptr);
}


//
// 'cupsdStartSelect()' - Initialize the file polling engine.
//

void
cupsdStartSelect(void)
{
  cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdStartSelect()");

  cupsd_fds = cupsArrayNew((cups_array_func_t)compare_fds, NULL);

  cupsd_update_pollfds = 0;
}


//
// 'cupsdStopSelect()' - Shutdown the file polling engine.
//

void
cupsdStopSelect(void)
{
  _cupsd_fd_t	*fdptr;			// Current file descriptor


  cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdStopSelect()");

  for (fdptr = (_cupsd_fd_t *)cupsArrayFirst(cupsd_fds); fdptr; fdptr = (_cupsd_fd_t *)cupsArrayNext(cupsd_fds))
    free(fdptr);

  cupsArrayDelete(cupsd_fds);
  cupsd_fds = NULL;

  if (cupsd_pollfds)
  {
    free(cupsd_pollfds);
    cupsd_pollfds       = NULL;
    cupsd_alloc_pollfds = 0;
  }

  cupsd_update_pollfds = 0;
}


//
// 'compare_fds()' - Compare file descriptors.
//

static int                  // O - Result of comparison
compare_fds(_cupsd_fd_t *a, // I - First file descriptor
            _cupsd_fd_t *b, // I - Second file descriptor
            void *data)     // I - Unused
{
  (void)data;
  return (a->fd - b->fd);
}


//
// 'find_fd()' - Find an existing file descriptor record.
//

static _cupsd_fd_t *			// O - FD record pointer or NULL
find_fd(int fd)				// I - File descriptor
{
  _cupsd_fd_t	*fdptr,			// Matching record (if any)
		key;			// Search key


  cupsArraySave(cupsd_fds);

  key.fd = fd;
  fdptr  = (_cupsd_fd_t *)cupsArrayFind(cupsd_fds, &key);

  cupsArrayRestore(cupsd_fds);

  return (fdptr);
}
