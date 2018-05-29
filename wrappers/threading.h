#ifndef COMMON_THREADING_H
#define COMMON_THREADING_H


#if   defined(_WIN32_WCE) || defined (_WINDOWS)
#include <windows.h>
#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#if defined(ANDROID) || defined(__ANDROID__)
#include <android/api-level.h>
#endif
#endif // OS switch

#include <string>
#include <stdint.h>

#define CST_TIMEOUT_INFINITE (-1L)

namespace cblp {

// TODO:
// thread diagnostcs

class Mutex
{
public:
    class ScopedLock
    {
    public:
        explicit ScopedLock(Mutex& mutex): _mutex(mutex) { _mutex.lock(); }
        ~ScopedLock() { _mutex.unlock(); }

    private:
        Mutex& _mutex;

        /// noncopyable
        ScopedLock();
        ScopedLock(const ScopedLock&);
        ScopedLock& operator = (const ScopedLock&) { return *this; }
    };

    enum
    {
        PRIO_INHERIT,
        PRIO_CEILING
    };

    Mutex(const char name[] = "", int priority_inversion_policy = PRIO_INHERIT);
    ~Mutex();

    void lock(bool* system_err = 0);
    void unlock(bool* system_err = 0);
    bool try_lock(long milliseconds = 0, bool* system_err = 0);

    const char * get_name() { return _name.c_str(); }
private:
    std::string _name;
#if   defined(_WIN32_WCE) || defined(_WINDOWS)
    HANDLE _mutex;
#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
    pthread_mutex_t _mutex;
#endif // OS switch
    /// noncopyable
    Mutex(const Mutex&);
    Mutex& operator = (const Mutex&);
};

class Semaphore
{
public:
    Semaphore(int n, int max, const char name[] = "");
    ~Semaphore();

    void set(bool * system_err = 0);
    bool wait(long milliseconds = CST_TIMEOUT_INFINITE, bool * system_err = 0);

    const char * get_name() { return _name.c_str(); }
private:
    std::string _name;
#if   defined(_WIN32_WCE) ||  defined (_WINDOWS)
    HANDLE _sema;
#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
    volatile int    _n;
    int             _max;
    pthread_mutex_t _mutex;
    pthread_cond_t  _cond;
#endif // OS switch

    /// noncopyable
    Semaphore();
    Semaphore(const Semaphore&);
    Semaphore& operator = (const Semaphore&);
};

class Thread
{
public:
#if   defined(_WIN32_WCE) || defined (_WINDOWS)
typedef DWORD threadid_t;
#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
typedef pthread_t threadid_t;
#endif // OS switch

    class Runnable
    {
        friend class Thread;
    public:
        Runnable() : _thread(0) {}
        virtual void thread_proc() = 0;
    protected:
        Thread * _thread;
        void set_owner(Thread * owner) { _thread = owner; }
        Thread * get_owner() { return _thread; }
    };

    enum
    {
        PRIO_HI,
        PRIO_NORMAL,
        PRIO_LOW,
        PRIO_INHERIT
    };

    Thread(Runnable* thread_obj, size_t stack_size = 64 * 1024, const char name[] = "", int prio = PRIO_NORMAL);
    ~Thread();
    int start(); //int priority = PRIO_INHERIT);
    int terminate();
    int join(int milliseconds = -1);

    const char * get_name() { return _name.c_str(); }
public:
    static void sleep(unsigned long milliseconds);
    static int64_t timestamp();
    static threadid_t threadid();
private:
    std::string _name; // for thread diagnostics
#if   defined(_WIN32_WCE) || defined (_WINDOWS)
    HANDLE       _thread;
#endif // OS switch
    threadid_t _thread_id;
    size_t _stack_size;
    int _prio;
    volatile bool _is_running;
    volatile bool _is_joined;
    Runnable * _thread_obj;
    Semaphore _sem;
    Mutex _mutex;

    static void* run(void* obj);
};

// TODO; add this methods to thread interface and implementation
//public:
//#if defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
//    void set_affinity(int);
//    int affinity();
//#endif // __unix
//    void set_priority(int);
//    int priority();
//    void set_stack_size();
//    int stack_size();

class ScopedRwLock;
class ScopedReadRwLock;
class ScopedWriteRwLock;

class RwLock
{
public:
    typedef ScopedRwLock ScopedLock;
    typedef ScopedReadRwLock ScopedReadLock;
    typedef ScopedWriteRwLock ScopedWriteLock;

    RwLock(const char name[] = "");
    ~RwLock();

    void read_lock(bool* system_err = 0);
    void write_lock(bool* system_err = 0);
    bool try_read_lock(bool* system_err = 0);
    bool try_write_lock(bool * system_err = 0);
    void unlock(bool* system_err = 0);

    const char * get_name() { return _name.c_str(); }
private:
    std::string _name;
#if   defined(_WIN32_WCE)
    DWORD _reader_count;
    DWORD _reader_waiting;
    DWORD _writer_count;
    DWORD _writer_waiting;
    HANDLE _reader_green;
    HANDLE _writer_green;
    CRITICAL_SECTION _cs;
    bool _write_lock;

    bool try_read_lock(DWORD timeout);
    bool try_write_lock(DWORD timeout);
#elif defined (_WINDOWS)
    HANDLE   _mutex;
    HANDLE   _readEvent;
    HANDLE   _writeEvent;
    unsigned _readers;
    unsigned _writersWaiting;
    unsigned _writers;

    void add_writer(bool* system_err);
    void remove_writer(bool* system_err);
    DWORD try_read_lock_once(bool* system_err);
#elif (defined(ANDROID) || defined(__ANDROID__)) && ( __ANDROID_API__ < 9 )
    pthread_mutex_t _mutex;
#elif defined(__unix) || defined(ANDROID) || defined(__ANDROID__)
    pthread_rwlock_t _rwl;
#endif // OS switch
    /// noncopyable
    RwLock(const RwLock&);
    RwLock& operator = (const RwLock&);
};

class ScopedRwLock
{
public:
    ScopedRwLock(RwLock& rwl, bool write = false)
        : _rwl(rwl)
    {
        if (write)
            _rwl.write_lock();
        else
            _rwl.read_lock();
    }

    ~ScopedRwLock() { _rwl.unlock(); }

private:
    RwLock& _rwl;

    ScopedRwLock();
    ScopedRwLock(const ScopedRwLock&);
    ScopedRwLock& operator = (const ScopedRwLock&);
};


class ScopedReadRwLock : public ScopedRwLock
{
public:
    ScopedReadRwLock(RwLock& rwl) : ScopedRwLock(rwl, false) {}
    ~ScopedReadRwLock() {}
};


class ScopedWriteRwLock : public ScopedRwLock
{
public:
    ScopedWriteRwLock(RwLock& rwl) : ScopedRwLock(rwl, true) {}
    ~ScopedWriteRwLock() {}
};

} // namespace cblp

#endif // COMMON_THREADING_H
