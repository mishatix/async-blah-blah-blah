#include <scheduler.h>
#include <stdio.h>

class TimerTest : public cblp::Timer
{
public:
    TimerTest(unsigned interval)
        : Timer(interval)
    {}

    virtual void run()
    {
        static int64_t a;

        if (a)
        {
            printf("interval %d, a=%d, timestamp=%d\n",
                   a - cblp::Thread::timestamp(), a, cblp::Thread::timestamp());
        }

        a = cblp::Thread::timestamp();
    }
};

int main(int, char**)
{
    TimerTest  A(1000);
    A.start();

    cblp::Thread::sleep(20000);
    return 0;
}
