#ifndef TCPSOCKET_H
#define TCPSOCKET_H

#include <common/common_utilities.h>
#include <common/threading.h>
#include <common/cst_memory.h>

#include <event2/util.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>

#if defined(_WINDOWS)
#include <windows.h>
#endif

//#include <list>
#include <set>
#include <stdint.h>

namespace cblp {

class LibeventLoop;

class TcpSocket
{
    friend class TcpServer;
    friend class LibeventLoop;

public:
    enum TimedOutOperation
    {
        Read    = 1,
        Write
    };

    class Handler
    {
        friend class TcpSocket;
    public:
        virtual void on_read(void* data) = 0;
        virtual void on_write(void* data) = 0;
        virtual void on_connect(void* data) = 0;
        virtual void on_timeout(void* data, TimedOutOperation op = Write) = 0;
        virtual void on_error(void* data) = 0;
        virtual void on_disconnect(void * data) { on_error(data); }

        virtual ~Handler() {}
    private:
        void on_read(TcpSocket * sock);
        void on_write(TcpSocket* sock);
        void on_io_event(TcpSocket* sock, short flags);
    };

public:
    TcpSocket(Handler * handler,
              LibeventLoop * proc,
              size_t outbuf_size = 64 * 1024,
              size_t inbuf_size = 64 * 1024);
    TcpSocket(Handler * handler,
              size_t outbuf_size = 64 * 1024,
              size_t inbuf_size = 64 * 1024);

    ~TcpSocket();

    void close();
    bool write(const void* buf, size_t size);
    bool send(const void* buf, size_t size, unsigned timeout = 0);
    size_t read(void* buf, size_t size);
    unsigned get_read_timeout() { return _read_timeout; }
    unsigned get_write_timeout() { return _write_timeout; }
    bool set_timeouts(unsigned r_timeout, unsigned w_timeout);
    bool is_valid() { return _is_valid; }
    bool is_connected() { return _is_connected; }

protected:
    Handler * _handler;
    LibeventLoop * _proc;

    bufferevent * _bufferevent;
    size_t _input_limit;
    size_t _output_limit;

    int _read_timeout;
    int _write_timeout;
    void * _cb_arg;
    volatile bool _is_valid;
    volatile bool _is_connected;

    void set_callback_arg(void * cb_arg) { _cb_arg = cb_arg;}
    bool open(size_t inbuf_size, void * callback_arg = 0);
    bool open(evutil_socket_t sock, size_t inbuf_size, void * callback_arg = 0);
    static void on_read(bufferevent *, void*);
    static void on_write(bufferevent *, void*);
    static void on_io_event(bufferevent *, short flags, void*);
};

class LibeventLoop : public Thread::Runnable
{
    friend class TcpSocket;
    friend class TcpServer;
public:
    LibeventLoop(bool start_immediatelly = false);
    ~LibeventLoop() { stop(); }

    event_base * get_event_base() { return (event_base*)_event_base; }
    bool start();
    void stop();

    Mutex& mutex() { return _mutex; }
    static LibeventLoop& common_instance();
protected:
    cblp::Thread   _thread;
    cblp::Mutex     _mutex;
    volatile bool _started;
    cblp::Thread::threadid_t _thread_id;
    volatile event_base * _event_base;
    evutil_socket_t _sock_pair[2];

    virtual void thread_proc();
    static void modification_callback_fn(evutil_socket_t sock, short flags, void * arg);
};

class TcpClient : public TcpSocket
{
public:
    TcpClient(TcpSocket::Handler * handler,
              LibeventLoop * proc,
              size_t outbuf_size = 64 * 1024,
              size_t inbuf_size = 64 * 1024);
    TcpClient(TcpSocket::Handler * handler,
              size_t outbuf_size = 64 * 1024,
              size_t inbuf_size = 64 * 1024);

    bool connect(uint32_t ip, uint16_t port, unsigned timeout = 0);
    bool connect(const char dns[], uint16_t port, unsigned timeout = 0);
};

class TcpServer
{
    friend class _Handler;
    friend class Connection;
    friend class TcpSocket;

public:
    class Handler : public TcpSocket::Handler
    {
        friend class TcpSocket;
    public:
        virtual void on_read(void* data) = 0;
        virtual void on_write(void* data) = 0;
        virtual void on_timeout(void* data,
                                TcpSocket::TimedOutOperation mode
                                = TcpSocket::Write)
        {
            (void) mode;
            Connection* con = (Connection*) data;
            con->get_owner()->remove_connection(data);
            con->close();
        }
        virtual void on_error(void* data)
        {
            Connection* con = (Connection*) data;
            con->_server->remove_connection(data);
            con->close();
        }

        virtual void on_connect(void* data) {
            Connection* con = (Connection*) data;
            con->_handler->on_connect(data);
        }
    };

    class Connection : public TcpSocket
    {
        friend class TcpServer;
        friend class TcpServer::Handler;
    public:
        Connection(TcpServer& server,
                   evutil_socket_t fd,
                   unsigned read_timeout, unsigned write_timeout,
                   unsigned outbuf_size = 64 * 1024,
                   unsigned inbuf_size = 64 * 1024);

        uint64_t get_id() { return _id; }
        TcpServer * get_owner() { return _server; }
        virtual void* self() { return this; }
    protected:
        TcpServer * _server;
        static uint64_t _id;
    };

public:
    TcpServer( TcpServer::Handler * handler, LibeventLoop * proc,
               size_t inbuf_size = 1024)
        : _handler(handler)
        , _proc(proc)
        , _listener(0)
    {}

    TcpServer( TcpServer::Handler * handler, size_t inbuf_size = 1024)
        : _handler(handler)
        , _proc(&LibeventLoop::common_instance())
        , _listener(0)
    {}

    ~TcpServer() { if (_listener) evconnlistener_free(_listener); }

    bool listen(uint32_t ip, uint16_t port);
    shared_ptr<Connection> find_connection(void* con);
    void remove_connection(void * con);
    void close();

private:
    static void accept_conn_cb(evconnlistener *listener,
        evutil_socket_t fd, sockaddr */*address*/, int /*socklen*/,
        void *ctx)
    {
        TcpServer * server = (TcpServer*) ctx;
        Mutex::ScopedLock guard(server->_proc->mutex());
        if (fd < 0)
            return;
        shared_ptr<Connection> new_client(
                server->allocate(server, fd, 0, 0, 64*1024, 64*1024)
                    );
        if ( !(new_client
               && new_client->_bufferevent
               && !bufferevent_enable(new_client->_bufferevent,
                                      EV_READ | EV_TIMEOUT| EV_WRITE)) )
                return;

         server->_clients.insert(
                     std::pair<void*, shared_ptr<Connection> >
                            (new_client.get(), new_client));
         server->_handler->on_connect(new_client.get());
    }

    static void accept_error_cb(evconnlistener *listener, void *ctx)
    {
        TcpServer * server = (TcpServer*) ctx;
        Mutex::ScopedLock guard(server->_proc->mutex());
        server->close();
    }


protected:
    Handler * _handler;
    LibeventLoop * _proc;
    typedef std::map< void*, shared_ptr<Connection> > Connections;
    Connections _clients;
    evconnlistener *_listener;

    virtual TcpServer::Connection * allocate(TcpServer * server,
                                  evutil_socket_t fd,
                                  unsigned read_timeout,
                                  unsigned write_timeout,
                                  unsigned outbuf_size = 64 * 1024,
                                  unsigned inbuf_size = 64 * 1024)
    {
        return new TcpServer::Connection(*server, fd, read_timeout, write_timeout,
                                         outbuf_size, inbuf_size);
    }
};

} // namespace cblp


#endif // TCPSOCKET_H
