
/*----------------------------------------------------------------------+
 |                                                                      |
 |      cplus.h - a loose collection of small C extensions              |
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

// Safe definition of statement-like macros
#define Statement(...) do{ __VA_ARGS__ }while(0)

#define quote(arg) #arg
#define quote2(arg) quote(arg)

#define arrayLen(a) (sizeof(a) / sizeof((a)[0]))

#if !defined(max)
#define max(a, b) ((a) >= (b) ? (a) : (b))
#endif

#if !defined(min)
#define min(a, b) ((a) <= (b) ? (a) : (b))
#endif

#define setMax(a, b) Statement( if ((a) < (b)) (a) = (b); )
#define setMin(a, b) Statement( if ((a) > (b)) (a) = (b); )

#define null      ((void*) 0)
#define constNull ((const void*) 0)

#define maxInt ((int)(~0U >> 1))
#define minInt (-maxInt - 1)

#define maxLong ((long)(~0UL >> 1))
#define minLong (-maxLong - 1L)

#define maxLongLong ((long long)(~0ULL >> 1))
#define minLongLong (-maxLongLong - 1LL)

#define ones(n) (~(~0ULL << (n)))

/*----------------------------------------------------------------------+
 |      Exceptions                                                      |
 +----------------------------------------------------------------------*/

struct xError {
        const char *format;
        const char *file;
        const char *function;
        int line;
        int argc;
        // struct xValue argv[];
};

typedef struct xError *err_t;

#define OK ((err_t) 0)

#define check(err) Statement(\
        if ((err) != OK)      \
                goto cleanup; \
)

err_t err_free(err_t err);

#define xRaise(msg) Statement(\
        static struct xError _static_err = {\
                .format = (msg),\
                .file = __FILE__,\
                .function = __func__,\
                .line = __LINE__,\
                .argc = -1\
        };\
        err = &_static_err;\
        goto cleanup;\
)

#define xAssert(cond) Statement(\
        if (!(cond))\
                xRaise("Assertion (" #cond ") failed");\
)

/*----------------------------------------------------------------------+
 |      Pairs / Tuples                                                  |
 +----------------------------------------------------------------------*/

#define Tuple(type, n)\
struct {\
        type v[n];\
}

#define Pair(type)              Tuple(type, 2)

typedef Pair(int)               intPair;

/*----------------------------------------------------------------------+
 |      Lists                                                           |
 +----------------------------------------------------------------------*/

#define List(type)\
struct {\
        type *v;\
        int len;\
        int maxLen;\
}

#define emptyList { null, 0, 0 }

typedef List(unsigned char) byteList;
typedef List(char)          charList;
typedef List(short)         shortList;
typedef List(int)           intList;
typedef List(long)          longList;
typedef List(uint64_t)      uint64List;

#define initialListSize (128)

// TODO: remove this one?
#define errPushList(list, value) Statement(\
        if ((list).len >= (list).maxLen) {\
                /* Avoid GCC warning "dereferencing type-punned pointer\
                   will break strict-aliasing rules" */\
                void *_v = (list).v;\
                err = list_ensure_len(\
                        &_v,\
                        &(list).maxLen,\
                        (list).len + 1,\
                        sizeof((list).v[0]),\
                        (initialListSize + sizeof((list).v[0]) - 1) / sizeof((list).v[0]));\
                check(err);\
                (list).v = _v;\
        }\
        (list).v[(list).len++] = (value); )

#define pushList(list, value) Statement(\
        if ((list).len >= (list).maxLen) {\
                /* Avoid GCC warning "dereferencing type-punned pointer\
                   will break strict-aliasing rules" */\
                void *_v = (list).v;\
                err_t _err = list_ensure_len(\
                        &_v,\
                        &(list).maxLen,\
                        (list).len + 1,\
                        sizeof((list).v[0]),\
                        (initialListSize + sizeof((list).v[0]) - 1) / sizeof((list).v[0]));\
                if (_err != OK)\
                        xAbort(_err);\
                (list).v = _v;\
        }\
        (list).v[(list).len++] = (value); )

#define popList(list) ((list).v[--(list).len])

#define freeList(list) Statement(       \
        if ((list).v) {                 \
                free((list).v);         \
                (list).v = null;        \
                (list).len = 0;         \
                (list).maxLen = 0;      \
        } )

err_t list_ensure_len(void **v, int *maxLen, int minLen, int unit, int newLen);

double xclock(void);
char *stringCopy(char *s, const char *t);
int readLine(void *fp, char **pLine, int *pSize);

/*----------------------------------------------------------------------+
 |      Main support                                                    |
 +----------------------------------------------------------------------*/

int xExitMain(err_t err);
void xAbort(err_t err);
void systemFailure(const char *function, int r);

/*----------------------------------------------------------------------+
 |      Threads and alarms                                              |
 +----------------------------------------------------------------------*/

// Use a dummy struct* instead of void* to provide some static type checking
typedef struct threadHandle *xThread_t;

// Definitions

typedef void thread_fn(void *data);

struct threadClosure {
        thread_fn *function;
        void *data;
};

struct alarm {
        thread_fn *function;
        void *data;
        double time;
};

// Functions

xThread_t setAlarm(struct alarm *alarm);
void clearAlarm(xThread_t alarm);

xThread_t createThread(struct threadClosure *thread);
void joinThread(xThread_t thread);

/*----------------------------------------------------------------------+
 |                                                                      |
 +----------------------------------------------------------------------*/

