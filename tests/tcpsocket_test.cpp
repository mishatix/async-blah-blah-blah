#include "common/tcpsocket.h"
#include "common/threading.h"
#include "common/scheduler.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <signal.h>
#include <iostream>
#include <string>

using namespace  CST;

static volatile bool started = true;

class ClientTester : public TcpSocket::Handler
{
public:
    virtual void on_read(void* data)
    {
        TcpSocket * sock = (TcpSocket*) data;
        char buf[1024];
        size_t len = sock->read(buf, sizeof buf);
        if (len)
        {
            std::string tmp(buf,len);
            std::cout << tmp;
        }
        else
        {
            sock->close();
            throw "Socket closed!!!";
        }
    }

    virtual void on_write(void* data)
    {
        (void) data;

    }

    virtual void on_connect(void* data)
    {
        (void) data;
        std::cout<< "Connected!!!" << std::endl;
    }

    virtual void on_error(void* data)
    {
        TcpSocket * sock = (TcpSocket*) data;
        std::cout<< "Socket error!!!" << std::endl;
        sock->close();
        exit(0);
    }

    virtual void on_disconnect(void* data)
    {
        TcpSocket * sock = (TcpSocket*) data;
        std::cout<< std::endl<<"Connection closed!!!" << std::endl;
        sock->close();
        exit(0);
    }



    virtual void on_timeout(void* data, TcpSocket::TimedOutOperation)
    {
        (void) data;

    }
};

ClientTester client_tester;
TcpClient tcpclient(&client_tester);

class EchoHandler1 : public TcpServer::Handler
{
public:
    virtual void on_read(void * data)
    {
        TcpServer::Connection * con = (TcpServer::Connection *) data;
        TcpSocket * sock = (TcpSocket*) data;
        char buf[1024];
        size_t len;
        do
        {
            len  = con->read(buf, sizeof buf);
            if (!con->write(buf, len))
            {
                con->get_owner()->remove_connection(data);
                con->close();
                return;
            }
        } while (len);
    }

    virtual void on_write(void *data) {}
};

class EchoTask : public Scheduler::Task
{
public:
    EchoTask(shared_ptr<TcpServer::Connection> con)
        : _con(con)
    {}

    virtual void exec(bool emergency_exit)
    {
        if (emergency_exit)
            return;

        char buf[1024];
        size_t len;
        do
        {
            len  = _con->read(buf, sizeof buf);
            if (!_con->write(buf, len))
            {
                _con->get_owner()->remove_connection(_con.get());
                _con->close();
                return;
            }
        } while (len);
    }
private:
    shared_ptr<TcpServer::Connection> _con;
};

class EchoHandler2 : public TcpServer::Handler
{
public:
    virtual void on_read(void * data)
    {
        TcpServer::Connection * con = (TcpServer::Connection *) data;
        shared_ptr<Scheduler::Task> task (new EchoTask(con->get_owner()->find_connection(data)));
        Scheduler::common_instance().schedule_task(task,0);
    }

    virtual void on_write(void *data) {}

};

void handler(int)
{
    tcpclient.close();
    started = false;
}


class TelnetClient
{
public:
    TelnetClient();
};

EchoHandler1 echo1;
EchoHandler2 echo2;

TcpServer tcpserver1(&echo1);
TcpServer tcpserver2(&echo2);


int str2ip(const char str[], sockaddr_in * sa)
{
    return evutil_inet_pton(AF_INET, str, &(sa->sin_addr));
}

int client(char addr[], char port[])
{
    sockaddr_in sa;
    str2ip(addr, &sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(strtoul(port, 0, 10));
    if (!sa.sin_port)
    {
        printf("Inorrect IPv4 addres or port\n");
        return 0;
    }

    uint32_t ad = ntohl(sa.sin_addr.s_addr);
    uint16_t p = ntohs(sa.sin_port);



    printf("addr is \"%u.%u.%u.%u\" port is %u\n",
           (ad >> 24) & 0xFF,
           (ad >> 16) & 0xFF,
           (ad >> 8) & 0xFF,
           ad & 0xFF,
           (unsigned)p);
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    if ( !tcpclient.connect(ad,p) )
    {
        printf("libevent connection error\n");
        return 0;
    }


    while (started)
    {
        char buf[1024];
        size_t len = 0;
        memset(buf, 0, sizeof buf);
        char c;

        do {
            c = getchar();
            if (c != '\r' && c !='\n' && tcpclient.is_connected())
            {
                buf[len++] = c;
                if (len >= sizeof buf)
                {
                    printf("buffer overflow\n");
                    return 0;
                }
            }

            break;
        } while ( c != 2 );

        if (c == 2) // Ctrl-C
            return 0;

        std::string tmp = buf;
        tmp = tmp + "\r\n";

        tcpclient.send(tmp.c_str(),tmp.size(), 0);
    }

    return 0;
}

int server1(char port[])
{
    uint16_t p = strtoul(port, 0, 10);
    if (!p)
    {
        printf("Inorrect port\n");
        return 0;
    }

    if (!tcpserver1.listen(0,p))
    {
        perror("");
        printf("Cannot bind port %u\n", unsigned(p));
        return 0;
    }

    while (started)
    {
        Thread::sleep(100);
    }

    return 0;
}

int server2(char port[])
{
    uint16_t p = strtoul(port, 0, 10);
    if (!p)
    {
        printf("Inorrect port\n");
        return 0;
    }

    if (!tcpserver2.listen(0,p))
    {
        perror("");
        printf("Cannot bind port %u\n", unsigned(p));
        return 0;
    }

    while (started)
    {
        Thread::sleep(100);
    }

    return 0;
}

int help(char name[])
{
    int len = strlen(name);
    char * n = 0;
    for (int i = len - 1; i >= 0; i--)
    {
        if (name[i] == '/')
        {
            n = &name[i+1];
            break;
        }
    }

    printf("Usage:\n"
           "    %s --client <addr> <port> -- telnet client\n"
           "    %s --server1 <port> -- one thread echo server\n"
           "    %s --server2 <port> -- two thread echo server\n"
           , n, n, n );
    return 0;
}


int main(int argc, char* argv[])
{
#ifdef _WINDOWS
    signal(SIGTERM, handler);
#else
    signal(SIGINT, handler);
#endif
    LibeventLoop::common_instance();
    Thread::sleep(1000);
    if (argc < 3)
        return help(argv[0]);


    if (argc >= 4 && !strncmp(argv[1], "--client", strlen("--client") + 1))
        return client(argv[2], argv[3]);
    else if (argc >= 3 && !strncmp(argv[1], "--server1", strlen("--server1") + 1))
        return server1(argv[2]);
    else if (argc >= 3 && !strncmp(argv[1], "--server2", strlen("--server2") + 1))
        return server2(argv[2]);

    return help(argv[0]);
}

