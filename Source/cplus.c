
/*----------------------------------------------------------------------+
 |                                                                      |
 |      cplus.c -- a loose collection of small C extensions             |
 |                                                                      |
 +----------------------------------------------------------------------*/

/*
 *  Copyright (C) 2015, Marcel van Kervinck
 *  All rights reserved
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*----------------------------------------------------------------------+
 |      Includes                                                        |
 +----------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
 #include <windows.h>
 #include <process.h>
 #include <sys/timeb.h>
#elif defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
 #include <pthread.h>
 #include <sys/time.h>
 #include <unistd.h>
 #define POSIX
#endif

#include "cplus.h"

/*----------------------------------------------------------------------+
 |      Exceptions                                                      |
 +----------------------------------------------------------------------*/

err_t err_free(err_t err)
{
        if (err->argc >= 0) {
#if 0
                for (int i=0; i<=err->argc; i++) {
                        xUnlink(err->argv[i]);
                }
#endif
                free(err);
        }
        return OK;
}

/*----------------------------------------------------------------------+
 |      Main support                                                    |
 +----------------------------------------------------------------------*/

int errExitMain(err_t err)
{
        if (err == OK) {
                return 0;
        } else {
                (void) fprintf(stderr, "[%s:%s:%d] Error: %s\n",
                        err->file,
                        err->function,
                        err->line,
                        err->format);

                (void) err_free(err);

                return EXIT_FAILURE;
        }
}

void errAbort(err_t err)
{
        (void) errExitMain(err);
        abort();
}

void xAbort(int r, const char *function)
{
        fprintf(stderr, "*** System error: %s failed (%s)\n", function, strerror(r));
        abort();
}

/*----------------------------------------------------------------------+
 |      Lists                                                           |
 +----------------------------------------------------------------------*/

/*
 *  Ensure a mimimum capacity for a list before it needs resizing.
 *  The resulting size will be rounded up in exponential manner to
 *  amortize the costs of repeatedly adding single items.
 *      list:      Pointer to list object
 *      itemSize:  Item size in bytes
 *      minLen:    Minimum number of required items. Must be at least list->len
 *      minSize:   Minimum size in bytes of a non-empty list
 */
err_t listEnsureMaxLen(voidList *list, int itemSize, int minLen, int minSize)
{
        err_t err = OK;
        assert(minLen >= list->len);
        assert(minLen > 0);

        if ((list->maxLen == 0) && (list->v != null))
                xRaise("Invalid operation on fixed-length list");

        int newMax = max(1, (minSize + itemSize - 1) / itemSize);
        while (newMax < minLen)
                newMax *= 2; // TODO: make this robust for huge lists (overflows etc)

        if (newMax != list->maxLen) {
                void *v = realloc(list->v, newMax * itemSize);
                if (v == null) xRaise("Out of memory");
                list->v = v;
                list->maxLen = newMax;
        }
cleanup:
        return err;
}

/*----------------------------------------------------------------------+
 |      xTime                                                           |
 +----------------------------------------------------------------------*/

/*
 *  Get wall time in seconds and subseconds
 */
double xTime(void)
{
#if defined(WIN32)
        struct _timeb t;
        _ftime(&t);
        return t.time + t.millitm * 1e-3;
#endif
#if defined(POSIX)
        struct timeval tv;
        int r = gettimeofday(&tv, null);
        if (r == 0)
                return tv.tv_sec + tv.tv_usec * 1e-6;
        else
                return -1.0;
#endif
}

/*----------------------------------------------------------------------+
 |      stringCopy                                                      |
 +----------------------------------------------------------------------*/

char *stringCopy(char *s, const char *t)
{
        while ((*s = *t)) {
                s++;
                t++;
        }
        return s; // give pointer to terminating zero for easy concatenation
}

/*----------------------------------------------------------------------+
 |      readline                                                        |
 +----------------------------------------------------------------------*/

int readLine(void *fpPointer, char **pLine, int *pSize)
{
        FILE *fp = fpPointer;
        char *line = *pLine;
        int size = *pSize;
        int len = 0;

        for (;;) {
                /*
                 *  Ensure there is enough space for the next character and a terminator
                 */
                if (len + 1 >= size) {
                        int newsize = (size > 0) ? (2 * size) : 128;
                        char *newline = realloc(line, newsize);

                        if (newline == NULL) {
                                fprintf(stderr, "*** Error: %s\n", strerror(errno));
                                exit(EXIT_FAILURE);
                        }

                        line = newline;
                        size = newsize;
                }

                /*
                 *  Process next character from file
                 */
                int c = getc(fp);
                if (c == EOF) {
                        if (ferror(fp)) {
                                fprintf(stderr, "*** Error: %s\n", strerror(errno));
                                exit(EXIT_FAILURE);
                        } else {
                                break;
                        }
                }
                line[len++] = c;

                if (c == '\n') break; // End of line found
        }

        line[len] = '\0';
        *pLine = line;
        *pSize = size;

        return len;
}

/*----------------------------------------------------------------------+
 |      Threads (Windows)                                               |
 +----------------------------------------------------------------------*/
#if defined(_WIN32)

struct threadClosure {
        thread_fn *function;
        void *data;
};

static unsigned int __stdcall threadStart(void *args)
{
        struct threadClosure closure = *(struct threadClosure*)args;
        free(args);
        closure.function(closure.data);
        return 0;
}

xThread_t createThread(thread_fn *function, void *data)
{
        // We can't pass these on the stack due to the race-condition
        struct threadClosure *closure = malloc(sizeof(closure));
        if (!closure)
                xAbort(errno, "malloc");
        closure->function = function;
        closure->data = data;

        HANDLE threadHandle = (HANDLE) _beginthreadex(
                null, 0, threadStart, closure, 0, null);

        return threadHandle;
}

void joinThread(xThread_t thread)
{
        HANDLE threadHandle = (HANDLE) thread;
        WaitForSingleObject(threadHandle, INFINITE);
        CloseHandle(threadHandle);
}
#endif

/*----------------------------------------------------------------------+
 |      Threads (POSIX)                                                 |
 +----------------------------------------------------------------------*/
#if defined(POSIX)

struct threadClosure {
        thread_fn *function;
        void *data;
};

static void *threadStart(void *args)
{
        struct threadClosure closure = *(struct threadClosure*)args;
        free(args);
        closure.function(closure.data);
        return null;
}

xThread_t createThread(thread_fn *function, void *data)
{
        // We can't pass these on the stack due to the race-condition
        struct threadClosure *closure = malloc(sizeof(closure));
        if (!closure)
                xAbort(errno, "malloc");
        closure->function = function;
        closure->data = data;

        pthread_t threadHandle;
        int r = pthread_create(&threadHandle, null, threadStart, closure);
        cAbort(r, "pthread_create");

        return (xThread_t) threadHandle;
}

void joinThread(xThread_t thread)
{
        pthread_t threadHandle = (pthread_t) thread;
        int r = pthread_join(threadHandle, null);
        cAbort(r, "pthread_join");
}
#endif


/*----------------------------------------------------------------------+
 |      Alarms (Windows)                                                |
 +----------------------------------------------------------------------*/
#if defined(_WIN32)

// TODO: check and abort on all error conditions of win32 functions

struct alarmHandle {
        double delay;
        thread_fn *function;
        void *data;
        HANDLE event;
        HANDLE thread;
};

static unsigned int __stdcall alarmThreadStart(void *args)
{
        struct alarmHandle *alarm = args;
        DWORD millis = ceil(alarm->delay * 1e3);
        DWORD r = WaitForSingleObject(alarm->event, millis);
        if (r == WAIT_TIMEOUT)
                alarm->function(alarm->data);
        return 0;
}

xAlarm_t setAlarm(double delay, thread_fn *function, void *data)
{
        struct alarmHandle *alarm = malloc(sizeof(*alarm));
        if (!alarm) xAbort(errno, "malloc");

        alarm->delay = delay;
        alarm->function = function;
        alarm->data = data;
        alarm->event = CreateEvent(null, true, false, null);
        alarm->thread = (HANDLE) _beginthreadex(
                null, 0, alarmThreadStart, alarm, 0, null);

        return alarm;
}

void clearAlarm(xAlarm_t alarm)
{
        if (alarm) {
                SetEvent(alarm->event);
                WaitForSingleObject(alarm->thread, INFINITE);
                CloseHandle(alarm->thread);
                CloseHandle(alarm->event);
                free(alarm);
        }
}
#endif

/*----------------------------------------------------------------------+
 |      Alarms (POSIX)                                                  |
 +----------------------------------------------------------------------*/
#if defined(POSIX)

struct alarmHandle {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        struct timespec abstime;
        bool abort;
        thread_fn *function;
        void *data;
        pthread_t thread;
};

static void *alarmThreadStart(void *args)
{
        struct alarmHandle *alarm = args;
        int r;

        r = pthread_mutex_lock(&alarm->mutex);
        cAbort(r, "pthread_mutex_lock");

        while (!alarm->abort) {
                r = pthread_cond_timedwait(&alarm->cond, &alarm->mutex, &alarm->abstime);
                if (r == ETIMEDOUT)
                        break;
                cAbort(r, "pthread_cond_timedwait");
        }

        r = pthread_mutex_unlock(&alarm->mutex);
        cAbort(r, "pthread_mutex_unlock");

        if (!alarm->abort)
                alarm->function(alarm->data);

        return null;
}

xAlarm_t setAlarm(double delay, thread_fn *function, void *data)
{
        struct alarmHandle *alarm = malloc(sizeof(*alarm));
        if (!alarm) xAbort(errno, "malloc");

        int r = pthread_mutex_init(&alarm->mutex, null);
        cAbort(r, "pthread_mutex_init");

        r = pthread_cond_init(&alarm->cond, null);
        cAbort(r, "pthread_cond_init");

        double fabstime = xTime() + delay;
        alarm->abstime.tv_sec = fabstime;
        alarm->abstime.tv_nsec = fmod(fabstime, 1.0) * 1e9;

        alarm->abort = false;

        alarm->function = function;
        alarm->data = data;

        r = pthread_create(&alarm->thread, null, alarmThreadStart, alarm);
        cAbort(r, "pthread_create");

        return alarm;
}

void clearAlarm(xAlarm_t alarm)
{
        if (alarm == null)
                return;

        /*
         *  Stop alarm thread if it is still waiting
         */

        int r = pthread_mutex_lock(&alarm->mutex);
        cAbort(r, "pthread_mutex_lock");

        alarm->abort = true;
        r = pthread_cond_signal(&alarm->cond);
        cAbort(r, "pthread_cond_signal");

        r = pthread_mutex_unlock(&alarm->mutex);
        cAbort(r, "pthread_mutex_unlock");

        /*
         *  Free resources
         */

        r = pthread_join(alarm->thread, null);
        cAbort(r, "pthread_join");

        r = pthread_mutex_destroy(&alarm->mutex);
        cAbort(r, "pthread_mutex_destroy");

        r = pthread_cond_destroy(&alarm->cond);
        cAbort(r, "pthread_cond_destroy");

        free(alarm);
}
#endif

/*----------------------------------------------------------------------+
 |                                                                      |
 +----------------------------------------------------------------------*/

