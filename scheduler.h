#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "wrapers/threading.h"
#include "wrapers/memory.h"

#include <list>
#include <utility>
#include <stdint.h>
#include <time.h>

#include <stdio.h>

namespace cblp {

class Scheduler : public Thread::Runnable
{
public:
    class Task
    {
    public:
        virtual void exec(bool emergency_exit) = 0;
		virtual ~Task() {}
    };

    Scheduler(size_t stack_size, const char * thread_name = "");
    ~Scheduler();

    static Scheduler& common_instance()
    {
        static Scheduler instance(256*1024, "Scheduler common instance thread");
        return instance;
    }

    bool schedule_task(shared_ptr<Task> task, unsigned timeout);
    void unschedule_task(shared_ptr<Task> task);

private:
    typedef std::pair<shared_ptr<Task>, int64_t> TaskEntry;
    typedef std::list<TaskEntry> TaskQueue;

    TaskQueue _task_queue;
    volatile int _modifications_number;

    Semaphore _sema;
    Mutex _mutex;
    Thread _thread;

    volatile bool _is_running;

    // Thread::Runnable
    virtual void thread_proc();
};

// TODO:
//    struct Event
//    {
//        uintptr_t event_id;
//        uintptr_t x;
//        uintptr_t y;
//        uintptr_t z;
//    };
//
//    class EventListener
//    {
//    public:
//        virtual bool handle() = 0;
//
//        void register_interest(int event, int event_mask, Scheduler& scheduler = Scheduler::common_instance() );
//        void unregister_interest(int event, int event_mask, Scheduler& scheduler = Scheduler::common_instance() );
//    };

class Timer
{
public:
    enum Mode
    {
        Periodic,
        OneShot
    };

    Timer(unsigned interval,
          Mode mode = Periodic,
          Scheduler& context = Scheduler::common_instance());
    virtual ~Timer();

    void set_interval(unsigned x ) { _interval = x; }
    bool start();
    void stop();

    virtual void run()=0;

private:
    class Task : private Scheduler::Task
    {
    public:
        virtual void exec(bool emergency_exit);
        static Scheduler::Task* allocate(Timer*);
    private:
        Timer * _timer;
    };

    friend class Timer::Task;

    shared_ptr<Scheduler::Task> _exec;
    unsigned _interval;
    Mode _mode;
    Scheduler * _context;
};

} // namespace cblp

#endif // SCHEDULER_H
