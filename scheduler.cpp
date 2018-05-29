#include "scheduler.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

namespace cblp {

Scheduler::Scheduler(size_t stack_size, const char *thread_name)
    : _modifications_number(0)
    ,_sema(0, INT_MAX, "")
    , _mutex("")
    , _thread(reinterpret_cast<Thread::Runnable*>(this), stack_size, thread_name)
    , _is_running(true)
{
    if ( _thread.start() < 0 )
    {
        _is_running = false;
#ifdef __unix
        perror("Scheduler::Scheduler()");
#else
        printf("%s: Some system error!!!", __FUNCTION__);
#endif
        abort();
    }
}

Scheduler::~Scheduler()
{
    {
     Mutex::ScopedLock guard(_mutex);
    _is_running = false;
    _sema.set();
    }

    _thread.join(1000);
}

bool Scheduler::schedule_task(shared_ptr<Task> task, unsigned timeout)
{
    Mutex::ScopedLock guard(_mutex);
    if (!_is_running)
        return false;
    TaskEntry new_te(task, Thread::timestamp() + timeout);

    for (TaskQueue::iterator it = _task_queue.begin(); it != _task_queue.end(); ++it)
    {
        TaskEntry te = *it;
        if (new_te.second - te.second >= 0)
            continue;
        _task_queue.insert(it, new_te);
        _modifications_number++;
        _sema.set();
        return true;
    }
    _task_queue.push_back(new_te);
    assert (_modifications_number < INT_MAX);
    _modifications_number++;
    _sema.set();
    return true;
}

void Scheduler::unschedule_task(shared_ptr<Task> task)
{
    Mutex::ScopedLock guard(_mutex);
    for (TaskQueue::iterator it = _task_queue.begin(); it != _task_queue.end();)
    {
        TaskEntry te = *it;
        if (te.first.get() == task.get())
            it = _task_queue.erase(it);
        else
            ++it;
    }
}


/*virtual*/ void Scheduler::thread_proc()
{
    {
     Mutex::ScopedLock guard(_mutex);
    _is_running = true;
    }

    while (_is_running)
    {
        long timeout = CST_TIMEOUT_INFINITE;

        { // timeout calculation

        Mutex::ScopedLock guard(_mutex);
        if (!_task_queue.empty())
        {
            TaskEntry task_entry = _task_queue.front();
            timeout = task_entry.second - Thread::timestamp();
#if defined (DEBUG)
			printf("%s: tqe timeout is %ld\n", __FUNCTION__, timeout);
#endif
            if (timeout < 0)
                timeout = 0;

#if defined (DEBUG)
			for (TaskQueue::iterator it = _task_queue.begin(); it != _task_queue.end(); it++) {
				printf("%s:%i task %p, %u\n", __func__, __LINE__, it->first.get(), it->second - Thread::timestamp());
			}
#endif
        }

        } // timeout calculation

#if defined (DEBUG)
        printf("%s: timeout is %ld\n", __FUNCTION__, timeout);
#endif

        bool modified = _sema.wait(timeout);
        _mutex.lock();
        if (!modified)
        {
            _mutex.unlock();
            shared_ptr<Task> task;
            int64_t exectime;
            bool empty;

            do {

                {

                Mutex::ScopedLock guard(_mutex);
                if (_task_queue.empty())
                    break;

                TaskEntry task_entry = _task_queue.front();
                exectime = task_entry.second;
				if ( Thread::timestamp() - exectime < 0 )
					break;

                _task_queue.pop_front();
                task = task_entry.first;
                empty = _task_queue.empty();

                }

                task.get()->exec(false);
            } while ( Thread::timestamp() - exectime > 0 && !empty);
        }
        else
        {
            _modifications_number--;
            _mutex.unlock();
        }
    } // while (_is_running)

    // Task queue cleanup
    while (!_task_queue.empty())
    {
        shared_ptr<Task> task;

        {

        Mutex::ScopedLock guard(_mutex);
        TaskEntry task_entry = _task_queue.front();
        _task_queue.pop_front();
        task = task_entry.first;

        }

        task.get()->exec(true);
    }
}

Timer::Timer(unsigned interval, Mode mode, Scheduler &context)
    : _interval(interval)
    , _mode(mode)
    , _context(&context)
{
    _exec = shared_ptr<Scheduler::Task> (Timer::Task::allocate(this));
}

Timer::~Timer()
{
    stop();
}

bool Timer::start()
{
    return _context->schedule_task(_exec, _interval);
}

void Timer::stop()
{
    _context->unschedule_task(_exec);
}

/*static*/ Scheduler::Task* Timer::Task::allocate(Timer* timer)
{
    Timer::Task * task  = new Timer::Task; // ignore exception :)
    if (!task) return 0;
    task->_timer = timer;
    return task;
}

/*virtual*/ void Timer::Task::exec(bool emergency_exit)
{
    if (emergency_exit || !_timer)
        return;
     _timer->run();
     if (_timer->_mode == Timer::Periodic)
         _timer->_context->schedule_task(_timer->_exec, _timer->_interval);
}

} // namespace cblp

