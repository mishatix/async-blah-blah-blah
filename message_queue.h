#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include "threading.h"
#include <list>
#include <limits.h>

namespace cblp {

template<class Message>
class MessageQueue
{
public:
    MessageQueue()
        : _sema(0, INT_MAX, "")
        , _mutex("")
    {}

    bool receive(Message& message, long timeout)
    {
        if (_sema.wait(timeout))
        {
            message = _messages.front();
            _messages.pop_front();
            return true;
        }

        return false;
    }

    void send(const Message& m)
    {
        _mutex.lock();
        _messages.push_back(m);
        _mutex.unlock();

        _sema.set();
    }

private:
    Semaphore _sema;
    Mutex _mutex;
    std::list<Message> _messages;
};

// TODO: add finite length message queue with intrusive interface

} // namespace cblp

#endif // MESSAGE_QUEUE_H
