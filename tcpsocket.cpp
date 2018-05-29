#include "tcpsocket.h"

#include <event2/thread.h>
#ifndef _WINDOWS
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#else
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>

#ifdef _WINDOWS
#define TEMP_FAILURE_RETRY(__expr) __expr;
#endif

#ifndef _WINDOWS
#define SYSCALL_FAILURE_RETRY(__expr, __ret) \
                    { \
                        int __err; \
                        do { \
                            __err = __expr; \
                        } while ( __err < 0 && errno == EINTR ); \
                        __ret = __err; \
                    }
#else
#define SYSCALL_FAILURE_RETRY(ret, expression) { ret = expression; }
#endif
static void block_sigpipe()
{
#ifndef _WINDOWS
    static int flag = 0;
    if (flag)
        return;

    signal(SIGPIPE, SIG_IGN);
    flag = 1;
#endif
}

static void evthread_enable()
{
#ifndef _WINDOWS
        evthread_use_pthreads();
#else
        evthread_use_windows_threads();
#endif
}

namespace cblp {

TcpSocket::TcpSocket(Handler * handler,
          LibeventLoop * proc,
          size_t outbuf_size /* = 64 * 1024 */,
          size_t inbuf_size /* = 64 * 1024 */)
    : _handler(handler)
    , _proc(proc)
    , _bufferevent(0)
    , _input_limit(inbuf_size)
    , _output_limit(outbuf_size)
    , _write_timeout(0)
    , _read_timeout(0)
    , _is_valid(false)
    , _is_connected(false)
{
}

TcpSocket::TcpSocket(Handler * handler,
          size_t outbuf_size /* = 64 * 1024 */,
          size_t inbuf_size /* = 64 * 1024 */)
    : _handler(handler)
    , _proc(&LibeventLoop::common_instance())
    , _bufferevent(0)
    , _input_limit(inbuf_size)
    , _output_limit(outbuf_size)
    , _write_timeout(0)
    , _read_timeout(0)
{
}

TcpSocket::~TcpSocket()
{
    close();
}

bool TcpSocket::open(evutil_socket_t sock, size_t inbuf_size, void *cb_arg)
{
    Mutex::ScopedLock guard(_proc->mutex());
    if (_bufferevent)
        return false;

    _bufferevent = bufferevent_socket_new(_proc->get_event_base(), sock /* *(new evutil_socket_t )*/,
                                          BEV_OPT_CLOSE_ON_FREE);
//                                          | BEV_OPT_THREADSAFE);
//                                          | BEV_OPT_UNLOCK_CALLBACKS );
    if (!_bufferevent)
        return false;
    _cb_arg = cb_arg ? cb_arg : this;
    _input_limit = inbuf_size;
    bufferevent_setwatermark(_bufferevent,EV_READ, 0, inbuf_size);
    bufferevent_setcb(_bufferevent, on_read, on_write, on_io_event, this);
    bufferevent_enable(_bufferevent, EV_READ | EV_TIMEOUT);

    _is_valid = true;
    return true;
}

bool TcpSocket::open(size_t inbuf_size, void *callback_arg)
{
    return open(-1, inbuf_size, callback_arg);
}

void TcpSocket::close()
{
    Mutex::ScopedLock guard(_proc->mutex());

    if (_bufferevent)
        bufferevent_free(_bufferevent);
    _bufferevent = 0;
    _is_valid = false;
    _is_connected = false;
}

bool TcpSocket::write(const void* buf, size_t size)
{
    Mutex::ScopedLock guard(_proc->mutex());

    //bufferevent_lock(_bufferevent);
    evbuffer * output = bufferevent_get_output(_bufferevent);
    size_t len = evbuffer_get_length(output);
    if (_output_limit && size + len > _output_limit)
    {
        bufferevent_enable(_bufferevent, EV_READ | EV_TIMEOUT | EV_WRITE);
//        bufferevent_unlock(_bufferevent);
        return false;
    }

    if ( bufferevent_write(_bufferevent, buf, size) )
    {
        int err = EVUTIL_SOCKET_ERROR();
        fprintf(stderr, "Got an error %d (%s) on bufferevent_write\n "
                 , err, evutil_socket_error_to_string(err));
        // TODO: error handling
        // it's allmost impossible situation (not enough memory)
        //bufferevent_unlock(_bufferevent);
        return false;
    }

    return true;
}

bool TcpSocket::send(const void* buf, size_t size, unsigned timeout /*= 0*/)
{
    if ( set_timeouts(_read_timeout, timeout))
        return write(buf, size);
    return false;
}

size_t TcpSocket::read(void* buf, size_t size)
{
    Mutex::ScopedLock guard(_proc->mutex());
    if (!_bufferevent)
        return 0;

    size_t len = bufferevent_read(_bufferevent, buf, size);
    return len;
}

bool TcpSocket::set_timeouts(unsigned r_timeout, unsigned w_timeout)
{
    Mutex::ScopedLock guard(_proc->mutex());

    if (!_bufferevent)
        return false;

    timeval r_tv, w_tv;

    w_tv.tv_sec = w_timeout / 1000;
    w_tv.tv_usec = (w_timeout % 1000) * 1000;
    r_tv.tv_sec = r_timeout / 1000;
    r_tv.tv_usec = (r_timeout % 1000) * 1000;

    if (bufferevent_set_timeouts(_bufferevent,
                             r_timeout ? &r_tv : 0,
                             w_timeout ? &w_tv : 0) )
    {
        return false;
    }
    _read_timeout = r_timeout;
    _write_timeout = w_timeout;
    return true;
}

/*static*/ void TcpSocket::on_read(bufferevent *, void * arg)
{
    TcpSocket * sock = (TcpSocket*) arg;
    sock->_handler->on_read(sock);
}

/*static*/ void TcpSocket::on_write(bufferevent *, void * arg)
{
    TcpSocket * sock = (TcpSocket*) arg;
    sock->_handler->on_write(sock);
}

/*static*/ void TcpSocket::on_io_event(bufferevent *, short flags, void * arg)
{
    TcpSocket * sock = (TcpSocket*) arg;
    sock->_handler->on_io_event(sock, flags);
}

TcpClient::TcpClient(TcpSocket::Handler *handler,
                     LibeventLoop *proc,
                     size_t outbuf_size,
                     size_t inbuf_size)
    : TcpSocket(handler, proc, outbuf_size, inbuf_size)
{
}

void TcpSocket::Handler::on_read(TcpSocket * sock)
{
    Mutex::ScopedLock guard(sock->_proc->mutex());
    sock->_handler->on_read(sock->_cb_arg);
}

void TcpSocket::Handler::on_write(TcpSocket* sock)
{
    Mutex::ScopedLock guard(sock->_proc->mutex());
    sock->_is_connected = true;
    sock->_handler->on_write(sock->_cb_arg);
}

void TcpSocket::Handler::on_io_event(TcpSocket* sock, short flags)
{
    Mutex::ScopedLock guard(sock->_proc->mutex());
    TimedOutOperation op = (flags & BEV_EVENT_READING) ? Read: Write;
    if (flags & BEV_EVENT_ERROR)
        sock->_handler->on_error(sock->_cb_arg);
    else if (flags & BEV_EVENT_CONNECTED)
    {
        sock->_is_connected = true;
        sock->_handler->on_connect(sock->_cb_arg);
    }
    else if (flags & BEV_EVENT_TIMEOUT)
        sock->_handler->on_timeout(sock->_cb_arg, op);
    else if (flags & BEV_EVENT_EOF)
    {
        sock->_handler->on_disconnect(sock->_cb_arg);
        sock->close();
    }
}

TcpClient::TcpClient(TcpSocket::Handler *handler,
                     size_t outbuf_size,
                     size_t inbuf_size)
    : TcpSocket(handler, outbuf_size, inbuf_size)
{
}

bool TcpClient::connect(uint32_t ip, uint16_t port, unsigned timeout)
{
    Mutex::ScopedLock guard(_proc->mutex());
    if (!_bufferevent)
        open(_input_limit);
    if (!_bufferevent)
        return false;
    if (timeout && !set_timeouts(_read_timeout, timeout))
        return false;

    short flags = (_read_timeout || _write_timeout) ? EV_TIMEOUT : 0;
    if (bufferevent_enable(_bufferevent, EV_READ | EV_WRITE | flags))
        return false;
    sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ip);
    addr.sin_port = htons(port);

    volatile int fd = bufferevent_getfd(_bufferevent);

    if (bufferevent_socket_connect(_bufferevent, (sockaddr*)&addr, sizeof addr))
        return false;
    return true;
}

bool TcpClient::connect(const char dns[], uint16_t port, unsigned timeout)
{
    Mutex::ScopedLock guard(_proc->mutex());

    if (!_bufferevent)
        open(_input_limit);
    if (!_bufferevent)
        return false;

    if (timeout && !set_timeouts(_read_timeout, timeout))
        return false;

    short flags = (_read_timeout || _write_timeout) ? EV_TIMEOUT : 0;
    bufferevent_enable(_bufferevent, EV_READ | EV_WRITE | flags);

    if (bufferevent_socket_connect_hostname(_bufferevent,
                                            0 /*syncronous gethostbyname */,
                                            AF_UNSPEC, dns, port) )
        return false;

    return true;
}

LibeventLoop::LibeventLoop(bool start_immediatelly)
        : _thread(this, 256u * 1024u)
        , _started(false)
        , _event_base(0)
{
    if (start_immediatelly)
        start();
}

bool LibeventLoop::start()
{
    if (_started)
        return false;
    _started = true;
    if (!_thread.start())
    {
        while (!_event_base)
        {
            if (!_started)
                return false;
            Thread::sleep(10);
        }
        return true;
    }
    return _started = false;
}

void LibeventLoop::stop()
{
    _started = false;
    unsigned char buf;
    buf = 0xFF;
    TEMP_FAILURE_RETRY(write(_sock_pair[1], &buf, 1));
    _started = false;
}

LibeventLoop& LibeventLoop::common_instance()
{
    static LibeventLoop instance(true);
    return instance;
}

/*virtual*/ void LibeventLoop::thread_proc()
{
    event* _modification_event;
    {
    Mutex::ScopedLock guard(_mutex);
    _thread_id = Thread::threadid();
    block_sigpipe();
    evthread_enable();

    int err;
    SYSCALL_FAILURE_RETRY(
            evutil_socketpair(PF_LOCAL, SOCK_STREAM, 0, _sock_pair),
                err);
    if (err < 0) exit(1);
    SYSCALL_FAILURE_RETRY(evutil_make_socket_nonblocking(_sock_pair[0]), err);
    if (err < 0) exit(1);
    SYSCALL_FAILURE_RETRY(evutil_make_socket_nonblocking(_sock_pair[1]), err);
    if (err < 0) exit(1);

    _event_base = event_base_new();
    _modification_event = event_new((event_base*)_event_base, _sock_pair[0],
            EV_READ | EV_PERSIST, modification_callback_fn, this);
    event_add(_modification_event, 0);
    }

    while (_started)
    {
        event_base_dispatch((event_base*)_event_base);
    }

    event_del(_modification_event);
    event_free(_modification_event);

    event_base_free ((event_base*)_event_base);
    TEMP_FAILURE_RETRY(evutil_closesocket(_sock_pair[0]));
    TEMP_FAILURE_RETRY(evutil_closesocket(_sock_pair[1]));
}

/*static*/ void LibeventLoop::modification_callback_fn(evutil_socket_t, short flags, void * arg)
{
    LibeventLoop * proc = static_cast<LibeventLoop*>(arg);

    if (!(flags & EV_READ))
    {
        printf("%s: unknown error\n", __FUNCTION__);
        return;
    }
    event_base_loopbreak((event_base*)proc->_event_base);
}

TcpServer::Connection::Connection(TcpServer& server,
                                  int fd,
                                  unsigned read_timeout,
                                  unsigned write_timeout,
                                  unsigned outbuf_size,
                                  unsigned inbuf_size)
    : TcpSocket(server._handler, server._proc, outbuf_size, inbuf_size )
    , _server(&server)
{
    ++_id;
    _read_timeout = read_timeout;
    _write_timeout = write_timeout;
    open(fd, _input_limit, this);
}

bool TcpServer::listen(uint32_t ip, uint16_t port)
{
    Mutex::ScopedLock guard(_proc->mutex());

    if (_listener)
        return false;

    sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(ip);
    sin.sin_port = htons(port);

    _listener = evconnlistener_new_bind(_proc->get_event_base(), accept_conn_cb,
                     this, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
                     (struct sockaddr*)&sin, sizeof(sin));
    if (!_listener) {
        //perror("Couldn't create listener");
        return false;
    }

    evconnlistener_set_error_cb(_listener, accept_error_cb);

    return true;
}

shared_ptr<TcpServer::Connection> TcpServer::find_connection(void* con)
{
    Mutex::ScopedLock guard(_proc->mutex());
    Connections::iterator it =_clients.find(con);
    if (it != _clients.end())
        return (*it).second;
}

void TcpServer::remove_connection(void * con)
{
    Mutex::ScopedLock guard(_proc->mutex());
    Connections::iterator it =_clients.find(con);
    if (it != _clients.end())
        _clients.erase(it);
}

void TcpServer::close()
{
    Mutex::ScopedLock guard(_proc->mutex());
    if (_listener)
    {
        evconnlistener_free(_listener);
        _listener = 0;
    }
    if (!_clients.empty())
        _clients.clear();
}

#ifdef _WINDOWS
typedef socklen_t int;
#endif

uint64_t TcpServer::Connection::_id;

} // namespace cblp
