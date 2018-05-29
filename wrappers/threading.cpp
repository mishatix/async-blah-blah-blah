#include "threading.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#if   defined(_WIN32_WCE) || defined (_WINDOWS)
#define TEMP_FAILURE_RETRY(__expr) __expr;
#define SYSCALL_FAILURE_RETRY(ret, expression) { ret = expression; }
#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
#include <unistd.h>
#include <sys/time.h>

#define SYSCALL_FAILURE_RETURN(__expr)  \
{ \
    int __err; \
    do { \
        __err = __expr; \
    } while ( __err < 0 && errno == EINTR ); \
    if (__err) \
    { \
        perror(#__expr); \
        return -1; \
    } \
}

#endif // OS switch

#define SYSCALL_POLLING_CONSTANT 1000 // milliseconds

namespace cblp {


#if   defined(_WIN32_WCE) || defined (_WINDOWS)

Semaphore::Semaphore(int n, int max, const char name[])
    : _name(name)
{
    assert (n >= 0 && max > 0 && n <= max);

    _sema = CreateSemaphoreW(NULL, n, max, NULL);
    if (!_sema)
    {
        printf("system error: cannot create semaphore");
        abort();
    }
}

Semaphore::~Semaphore()
{
    CloseHandle(_sema);
}

void Semaphore::set(bool *system_err)
{
    if (!ReleaseSemaphore(_sema, 1, NULL))
    {
        if (system_err)
            *system_err = true;
    }

    if (system_err)
        *system_err = false;
}

bool Semaphore::wait(long milliseconds, bool *system_err)
{
    if (system_err)
        *system_err = false;

    if (milliseconds < 0)
    {
        switch (WaitForSingleObject(_sema, INFINITE))
        {
        case WAIT_OBJECT_0:
            return true;
        default:
            if (system_err)
                *system_err = true;
            return false
        }
    }
    else
    {
        switch (WaitForSingleObject(_sema, milliseconds + 1))
        {
        case WAIT_TIMEOUT:
            return false;
        case WAIT_OBJECT_0:
            return true;
        default:
            if (system_err)
                *system_err = true;
            return false
    }
}

#elif defined(ANDROID) || defined(__ANDROID__) || defined(__unix)

Semaphore::Semaphore(int n, int max, const char name[])
    : _name(name)
    , _n(n)
    , _max(max)
{
    if (pthread_mutex_init(&_mutex, NULL))
    {
        perror("pthread_mutex_init(&_mutex, NULL)");
        abort();
    };

    if (pthread_cond_init(&_cond, NULL))
    {
        perror("pthread_cond_init(&_cond, NULL)");
        abort();
    };
}

Semaphore::~Semaphore()
{
    pthread_cond_destroy(&_cond);
    pthread_mutex_destroy(&_mutex);
}

void Semaphore::set(bool *system_err)
{
    if (system_err)
        *system_err = false;

    if (pthread_mutex_lock(&_mutex))
    {
        if (system_err)
            *system_err = true;
        return;
    }

    if (_n < _max)
    {
        ++_n;
    }
    else
    {
        if (pthread_mutex_unlock(&_mutex))
        {
            if (system_err)
                *system_err = true;
            return;
        }
    }

    ;
    if (pthread_cond_signal(&_cond))
    {
        if (system_err)
            *system_err = true;
        pthread_mutex_unlock(&_mutex);
        return;
    }

    if (pthread_mutex_unlock(&_mutex) && system_err)
        *system_err = true;
}

bool Semaphore::wait(long milliseconds, bool *system_err)
{
    if (system_err)
        *system_err = false;

    if (milliseconds  < 0)
    {
        if (pthread_mutex_lock(&_mutex))
        {
            if (system_err)
                *system_err = true;
            return false;
        }

        while (_n < 1)
        {
            if (pthread_cond_wait(&_cond, &_mutex) && system_err)
            {
                if (system_err)
                    *system_err = true;
                pthread_mutex_unlock(&_mutex);
                return false;
            }
        }
        --_n;
        if (pthread_mutex_unlock(&_mutex) && system_err)
            *system_err = true;

        return true;
    }
    else
    {
        int rc = 0;
        struct timespec abstime;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        abstime.tv_sec  = tv.tv_sec + milliseconds / 1000;
        abstime.tv_nsec = tv.tv_usec*1000 + (milliseconds % 1000)*1000000;
        if (abstime.tv_nsec >= 1000000000)
        {
            abstime.tv_nsec -= 1000000000;
            abstime.tv_sec++;
        }

        if (pthread_mutex_lock(&_mutex))
        {
            if (system_err)
                *system_err = true;
            return false;
        }

        while (_n < 1)
        {
            if ((rc = pthread_cond_timedwait(&_cond, &_mutex, &abstime)))
            {
                if (rc == ETIMEDOUT) break;
                if (system_err)
                    *system_err = true;
                pthread_mutex_unlock(&_mutex);
                return false;
            }
        }

        if (rc == 0) --_n;
        if (pthread_mutex_unlock(&_mutex) && system_err)
            *system_err = true;
        return rc == 0;
    }
}

#endif // OS switch

#if   defined(_WIN32_WCE) || defined (_WINDOWS)

int Thread::start()
{
    _mutex.lock();
    if (_is_running)
    {
        _mutex.unlock();
        retrn -1;
    }
    _mutex.unlock();

    _thread = CreateThread(NULL, _stackSize, ent, pData, 0, &_thread_id);

    if (!_thread)
    {
        printf("CreateThread(NULL, _stackSize, ent, pData, 0, &_thread_id) error\n");
        return -1;
    }

    return 0;
}

int Thread::terminate()
{
    if (!TerminateThread(_thread, 0))
        return -1;

    _mutex.lock();
    _is_running = false;
    _mutex.unlock();
    _sem.set();
    return 0;
}

#elif defined(ANDROID) || defined(__ANDROID__) || defined(__unix)

int Thread::start()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, _stack_size);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    _thread_obj->set_owner(this);
    SYSCALL_FAILURE_RETURN(pthread_create(&_thread_id, &attr, Thread::run, this));
    pthread_attr_destroy(&attr);
    return 0;
}

int Thread::terminate()
{
    if (pthread_cancel(_thread_id))
        return -1;

    _mutex.lock();
    _is_running = false;
    _mutex.unlock();
    _sem.set();
    return 0;
}

#endif // OS switch
// TODO: bug fix (semaphore reinitialization on start of thread)
// in the case when thread was stopped, and then started
// call of join() leads to incorrect result
Thread::Thread(Runnable* thread_obj, size_t stack_size, const char name[], int prio)
    : _name(name)
    , _thread_id(0)
    , _stack_size(stack_size)
    , _prio(prio)
    , _is_running(false)
    , _thread_obj(thread_obj)
    , _sem(0,1, std::string(std::string(name)+std::string(" thread semaphore")).c_str())
    , _mutex(std::string(std::string(name)+std::string(" thread mutex")).c_str())
{
}

Thread::~Thread()
{
    if (_is_running)
        terminate();
}

int Thread::join(int milliseconds)
{
    _mutex.lock();
    if (_is_joined)
    {
        _mutex.unlock();
        return -1;
    }
    _is_joined = true;
    _mutex.unlock();

    int counter = milliseconds;
    while (counter > 0 || !milliseconds)
    {
        if ( _sem.wait(SYSCALL_POLLING_CONSTANT) )
            return 0;
        counter -= SYSCALL_POLLING_CONSTANT;
        if (!_is_running)
        {
            terminate();
            _mutex.lock();
            _is_joined = false;
            _mutex.unlock();
            return -1;
        }
    }

    return 0;
}

void * Thread::run(void *obj)
{
    Thread* _this = static_cast<Thread*>(obj);
    _this->_mutex.lock();
    _this->_is_running = true;
    _this->_mutex.unlock();
    _this->_thread_obj->thread_proc();
    _this->_mutex.lock();
    _this->_is_running = false;
    _this->_mutex.unlock();
    _this->_sem.set();
    return 0;
}


/*static*/ void Thread::sleep(unsigned long milliseconds)
{
#if   defined(_WIN32_WCE) || defined (_WINDOWS)
    ::Sleep(milliseconds);
#elif defined(__unix)
    usleep(1000 * milliseconds);
#endif
}

/*static*/ int64_t Thread::timestamp()
{
#if defined(_WINDOWS)
    LARGE_INTEGER li;
    LARGE_INTEGER result;
    QueryPerformanceFrequency(&li);
    QueryPerformanceCounter(&result);
    double freq = double(li.QuadPart) / 10000000.0;
    return (long long)(double(result.QuadPart) / freq);
#elif defined(__unix)
    struct timespec tm;
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &tm);
#else
    clock_gettime(CLOCK_REALTIME, &tm);
#endif
    return tm.tv_sec * 1000 + tm.tv_nsec / 1000000;
#else
    return 0;
#endif
}

/*static*/ Thread::threadid_t Thread::threadid()
{
#if defined(_WINDOWS)
    return GetCurrentThreadId();
#elif defined(__unix)
    return pthread_self();
#else
    return 0;
#endif
}

#if   defined(_WIN32_WCE) || defined (_WINDOWS)

Mutex::Mutex(const char name[], int /*priority_inversion_policy*/)
    : _name(name)
{
    _mutex = CreateMutexW(NULL, FALSE, NULL);
}

Mutex::~Mutex()
{
    CloseHandle(_mutex);
}

void Mutex::unlock(bool*)
{
    ReleaseMutex(_mutex);
}

void Mutex::lock(bool*)
{
    WaitForSingleObject(_mutex, INFINITE);
}

bool Mutex::try_lock(long milliseconds, bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (milliseconds <=0 )
    {
        switch (WaitForSingleObject(_mutex, 0))
        {
        case WAIT_TIMEOUT:
            return false;
        case WAIT_OBJECT_0:
            return true;
        default:
            if (system_err)
                *system_err = true;
            return false;
        }
    }

    switch (WaitForSingleObject(_mutex, milliseconds + 1))
    {
    case WAIT_TIMEOUT:
        return false;
    case WAIT_OBJECT_0:
        return true;
    default:
        if (system_err)
            *system_err = true;
        return false;
    }
}

#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)

Mutex::Mutex(const char name[], int /*priority_inversion_policy*/)
    : _name(name)
{
    memset(&_mutex, 0, sizeof(_mutex));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&_mutex, &attr))
    {
        pthread_mutexattr_destroy(&attr);
        perror("pthread_mutex_init(&_mutex, &attr)");
        abort();
    }
    pthread_mutexattr_destroy(&attr);
}

Mutex::~Mutex()
{
    pthread_mutex_destroy(&_mutex);
}

void Mutex::lock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_mutex_lock(&_mutex) && system_err)
        *system_err = true;
}

void Mutex::unlock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_mutex_unlock(&_mutex) && system_err)
        *system_err = true;
}

bool Mutex::try_lock(long milliseconds, bool *system_err)
{
    if (system_err)
        *system_err = false;

    if (milliseconds <= 0)
    {
        int rc = pthread_mutex_trylock(&_mutex);
        if (rc == 0)
            return true;
        else if (rc == EBUSY)
            return false;
        if (system_err)
            *system_err = true;
        return false;
    }

    struct timespec abstime;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    abstime.tv_sec  = tv.tv_sec + milliseconds / 1000;
    abstime.tv_nsec = tv.tv_usec*1000 + (milliseconds % 1000)*1000000;
    if (abstime.tv_nsec >= 1000000000)
    {
        abstime.tv_nsec -= 1000000000;
        abstime.tv_sec++;
    }
    int rc = pthread_mutex_timedlock(&_mutex, &abstime);
    if (rc == 0)
        return true;
    else if (rc == ETIMEDOUT)
        return false;

    if (system_err)
        *system_err = true;
    return false;
}

#endif // OS switch

#if   defined(_WIN32_WCE)  ///////////////////////////////////////////////////////////////////////////////////
// TODO:
RwLock::RwLock():
    _reader_count(0),
    _reader_waiting(0),
    _writer_count(0),
    _writer_waiting(0),
    _write_lock(false)
{
    InitializeCriticalSection(&_cs);
    _reader_green = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (!_reader_green) throw "Cannot create RwLock";
    _writer_green = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (!_writerGreen)
    {
        CloseHandle(_reader_green);
        throw "Cannot create RwLock";
    }
}

RwLock::~RwLock()
{
    CloseHandle(_reader_green);
    CloseHandle(_writer_green);
    DeleteCriticalSection(&_cs);
}

void RwLock::read_lock(bool*)
{
    try_read_lock(INFINITE);
}

bool RwLock::try_read_lock(bool*)
{
    return try_read_lock((DWORD)1);
}

bool RwLock::try_write_lock(bool*)
{
    return try_write_lock((DWORD)1);
}

bool RwLock::try_read_lock(DWORD timeout)
{
    bool wait = false;
    do
    {
        EnterCriticalSection(&_cs);
        if (!_writer_count && !_writer_waiting)
        {
            if (wait)
            {
                _reader_waiting--;
                wait = false;
            }
            _reader_count++;
        }
        else
        {
            if (!wait)
            {
                _reader_waiting++;
                wait = true;
            }
            ResetEvent(_reader_green);
        }
        LeaveCriticalSection(&_cs);
        if (wait)
        {
            if (WaitForSingleObject(_reader_green, timeout) != WAIT_OBJECT_0)
            {
                EnterCriticalSection(&_cs);
                _reader_waiting--;
                SetEvent(_reader_green);
                SetEvent(_writer_green);
                LeaveCriticalSection(&_cs);
                return false;
            }
        }
    }
    while (wait);

    return true;
}

void RwLock::write_lock(bool*)
{
    try_write_lock(INFINITE);
}

bool RwLock::try_write_lock(DWORD timeout)
{
    bool wait = false;

    do
    {
        EnterCriticalSection(&_cs);
        if (!_reader_count && !_writer_count)
        {
            if (wait)
            {
                _writer_waiting--;
                wait = false;
            }
            _writer_count++;
        }
        else
        {
            if (!wait)
            {
                _writer_waiting++;
                wait = true;
            }
            ResetEvent(_writer_green);
        }
        LeaveCriticalSection(&_cs);
        if (wait)
        {
            if (WaitForSingleObject(_writer_green, timeout) != WAIT_OBJECT_0)
            {
                EnterCriticalSection(&_cs);
                _writer_waiting--;
                SetEvent(_reader_green);
                SetEvent(_writer_green);
                LeaveCriticalSection(&_cs);
                return false;
            }
        }
    }
    while (wait);

    _write_lock = true;
    return true;
}


void RwLock::unlock(bool*)
{
    EnterCriticalSection(&_cs);

    if (_write_lock)
    {
        _write_lock = false;
        _writer_count--;
    }
    else
    {
        _reader_count--;
    }
    if (_writer_waiting)
        SetEvent(_writer_green);
    else if (_reader_waiting)
        SetEvent(_reader_green);

    LeaveCriticalSection(&_cs);
}

#elif defined (_WINDOWS) ////////////////////////////////////////////////////////////////////////////////////

RwLock::RwLock()
    : _readers(0)
    , _writers_waiting(0)
    , _writers(0)
{
    _mutex = CreateMutexW(NULL, FALSE, NULL);
    _read_event = CreateEventW(NULL, TRUE, TRUE, NULL);
    _write_event = CreateEventW(NULL, TRUE, TRUE, NULL);
}

RwLock::~RwLock()
{
    CloseHandle(_mutex);
    CloseHandle(_read_event);
    CloseHandle(_write_event);
}

void RwLock::add_writer(bool * system_err)
{
    if (system_err) *system_err = false;
    switch (WaitForSingleObject(_mutex, INFINITE))
    {
    case WAIT_OBJECT_0:
        if (++_writers_waiting == 1) ResetEvent(_read_event);
        ReleaseMutex(_mutex);
        break;
    default:
        if (system_err) *system_err = true;
    }
}

void RwLock::remove_writer(bool * system_err)
{
    if (system_err) *system_err = false;
    switch (WaitForSingleObject(_mutex, INFINITE))
    {
    case WAIT_OBJECT_0:
        if (--_writers_waiting == 0 && _writers == 0) SetEvent(_read_event);
        ReleaseMutex(_mutex);
        break;
    default:
        if (system_err) *system_err = true;
    }
}

void RwLock::read_lock(bool * system_err)
{
    if (system_err) *system_err = false;
    HANDLE h[2];
    h[0] = _mutex;
    h[1] = _read_event;
    switch (WaitForMultipleObjects(2, h, TRUE, INFINITE))
    {
    case WAIT_OBJECT_0:
    case WAIT_OBJECT_0 + 1:
        ++_readers;
        ResetEvent(_write_event);
        ReleaseMutex(_mutex);
        assert(_writers == 0);
        break;
    default:
        if (system_err) *system_err = true;
    }
}

bool RwLock::try_read_lock(bool * system_err)
{
    if (system_err) *system_err = false;
    for (;;)
    {
        bool err;
        if (_writers != 0 || _writers_waiting != 0)
            return false;

        DWORD result = try_read_lock_once(&err);
        if (err)
        {
            if (system_err) *system_err = true;
            return false;
        }
        switch (result)
        {
        case WAIT_OBJECT_0:
        case WAIT_OBJECT_0 + 1:
            return true;
        case WAIT_TIMEOUT:
            continue; // try again
        default:
            if (system_err) *system_err = true;
        }
    }
}

void RwLock::write_lock(bool * system_err)
{
    if (system_err) *system_err = false;
    bool err;
    add_writer(&err);
    if (err)
    {
        if (system_err) *system_err = true;
        return;
    }

    HANDLE h[2];
    h[0] = _mutex;
    h[1] = _write_event;
    switch (WaitForMultipleObjects(2, h, TRUE, INFINITE))
    {
    case WAIT_OBJECT_0:
    case WAIT_OBJECT_0 + 1:
        --_writersWaiting;
        ++_readers;
        ++_writers;
        ResetEvent(_read_event);
        ResetEvent(_write_event);
        ReleaseMutex(_mutex);
        assert(_writers == 1);
        break;
    default:
        remove_writer(&err);
        if (system_err) *system_err = true;
    }
}

bool RwLock::try_write_lock(bool * system_err)
{
    if (system_err) *system_err = false;
    bool err;
    add_writer(&err);
    if (err)
    {
        if (system_err) *system_err = true;
        return false;
    }
    HANDLE h[2];
    h[0] = _mutex;
    h[1] = _write_event;
    switch (WaitForMultipleObjects(2, h, TRUE, 1))
    {
    case WAIT_OBJECT_0:
    case WAIT_OBJECT_0 + 1:
        --_writers_waiting;
        ++_readers;
        ++_writers;
        ResetEvent(_read_event);
        ResetEvent(_write_event);
        ReleaseMutex(_mutex);
        assert(_writers == 1);
        return true;
    case WAIT_TIMEOUT:
        remove_writer(&err);
        if (err && system_err) *system_err = true;
        return false;
    default:
        remove_writer(&err);
        if (system_err) *system_err = true;
    }
}

void RwLock::unlock(bool * system_err)
{
    if (system_err) *system_err = false;
    switch (WaitForSingleObject(_mutex, INFINITE))
    {
    case WAIT_OBJECT_0:
        _writers = 0;
        if (_writers_waiting == 0) SetEvent(_read_event);
        if (--_readers == 0) SetEvent(_write_event);
        ReleaseMutex(_mutex);
        break;
    default:
        if (system_err) *system_err = true;
    }
}

DWORD RwLock::try_read_lock_once(bool * system_err)
{
    if (system_err) *system_err = false;
    HANDLE h[2];
    h[0] = _mutex;
    h[1] = _read_event;
    DWORD result = WaitForMultipleObjects(2, h, TRUE, 1);
    switch (result)
    {
    case WAIT_OBJECT_0:
    case WAIT_OBJECT_0 + 1:
        ++_readers;
        ResetEvent(_write_event);
        ReleaseMutex(_mutex);
        assert(_writers == 0);
        return result;
    case WAIT_TIMEOUT:
        return result;
    default:
        if (system_err) *system_err = true;
    }
}
#elif (defined(ANDROID) || defined(__ANDROID__)) && ( __ANDROID_API__ < 9 )  /////////////////////////////////
RwLock::RwLock(const char name[])
    : _name(name)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&_mutex, &attr))
    {
        pthread_mutexattr_destroy(&attr);
        perror("pthread_mutex_init(&_mutex, &attr)");
        abort();
    }
    pthread_mutexattr_destroy(&attr);
}

RwLock::~RwLock()
{
    pthread_mutex_destroy(&_mutex);
}

void RwLock::read_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_mutex_lock(&_mutex) && system_err)
        *system_err = true;
}

void RwLock::write_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_mutex_lock(&_mutex) && system_err)
        *system_err = true;
}

void RwLock::unlock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_rwlock_unlock(&_rwl) && system_err)
        *system_err = true;
}

bool RwLock::try_read_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;

    int rc = pthread_mutex_trylock(&_mutex);
    if (rc == 0)
        return true;
    else if (rc == EBUSY)
        return false;
    else if (system_err)
        *system_err = true;

    return false;
}

bool RwLock::try_write_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;

    int rc = pthread_mutex_trylock(&_mutex);
    if (rc == 0)
        return true;
    else if (rc == EBUSY)
        return false;
    else if (system_err)
        *system_err = true;

    return false;
}

#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)

RwLock::RwLock(const char name[])
    : _name(name)
{
    if (pthread_rwlock_init(&_rwl, NULL))
    {
        perror("pthread_rwlock_init(&_rwl, NULL)");
        abort();
    }
}

RwLock::~RwLock()
{
    pthread_rwlock_destroy(&_rwl);
}

void RwLock::read_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_rwlock_rdlock(&_rwl) && system_err)
        *system_err = true;
}

void RwLock::write_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_rwlock_wrlock(&_rwl) && system_err)
        *system_err = true;
}

void RwLock::unlock(bool *system_err)
{
    if (system_err)
        *system_err = false;
    if (pthread_rwlock_unlock(&_rwl) && system_err)
        *system_err = true;
}

bool RwLock::try_read_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;

    int rc = pthread_rwlock_tryrdlock(&_rwl);
    if (rc == 0)
        return true;
    else if (rc == EBUSY)
        return false;
    else if (system_err)
        *system_err = true;

    return false;
}

bool RwLock::try_write_lock(bool *system_err)
{
    if (system_err)
        *system_err = false;

    int rc = pthread_rwlock_trywrlock(&_rwl);
    if (rc == 0)
        return true;
    else if (rc == EBUSY)
        return false;
    else if (system_err)
        *system_err = true;

    return false;
}

#endif // OS switch


} // namespace cblp
