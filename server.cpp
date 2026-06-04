#include "server.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <format>
// сетевые/опросные вызовы POSIX
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#include "common.h"
#include "connection.h"


static constexpr uint64_t kIdleTimeoutMs = 5 * 1000;
static constexpr size_t   kWorkerThreads = 4;


void Server::makeNonBlocking(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        fatal("fcntl error");
        return;
    }
    flags |= O_NONBLOCK;
    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        fatal("fcntl error");
    }
}

Server::Server(uint16_t port)
    : pool_(kWorkerThreads), db_(pool_)
{
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        fatal("socket()");
    }
    int yes = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(port);
    addr.sin_addr.s_addr = ntohl(0);    // привязка к 0.0.0.0
    if (bind(listenFd_, (const sockaddr *)&addr, sizeof(addr))) {
        fatal("bind()");
    }

    makeNonBlocking(listenFd_);
    if (listen(listenFd_, SOMAXCONN)) {
        fatal("listen()");
    }
}

Server::~Server() {
    if (listenFd_ >= 0) {
        close(listenFd_);
    }
    // здесь Connection — полный тип, поэтому элементы unique_ptr корректно уничтожаются
}

void Server::accept() {
    struct sockaddr_in peer = {};
    socklen_t peerLen = sizeof(peer);
    int fd = ::accept(listenFd_, (struct sockaddr *)&peer, &peerLen);
    if (fd < 0) {
        logErrno("accept() error");
        return;
    }
    uint32_t ip = peer.sin_addr.s_addr;
    std::fputs(std::format("new client from {}.{}.{}.{}:{}\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(peer.sin_port)).c_str(), stderr);

    makeNonBlocking(fd);

    auto conn = std::make_unique<Connection>(fd);
    conn->wantRead = true;
    conn->lastActiveMs = nowMillis();
    idleList_.insertBefore(&conn->idleLink);

    if (connections_.size() <= static_cast<size_t>(fd)) {
        connections_.resize(fd + 1);
    }
    assert(!connections_[fd]);
    connections_[fd] = std::move(conn);
}

void Server::drop(Connection *conn) {
    int fd = conn->fd();
    (void)close(fd);
    conn->idleLink.unlink();
    connections_[fd].reset();       // освобождает Connection
}

int32_t Server::nextTimeoutMs() {
    uint64_t now = nowMillis();
    uint64_t soonest = static_cast<uint64_t>(-1);
    // ближайшее истечение простоя (голова списка — наименее активное соединение)
    if (!idleList_.isEmpty()) {
        Connection *conn = ownerOf(idleList_.next, Connection, idleLink);
        soonest = conn->lastActiveMs + kIdleTimeoutMs;
    }
    // ближайшее истечение TTL
    const TimerHeap &timers = db_.timers();
    if (!timers.isEmpty() && timers[0].deadline < soonest) {
        soonest = timers[0].deadline;
    }
    if (soonest == static_cast<uint64_t>(-1)) {
        return -1;
    }
    if (soonest <= now) {
        return 0;
    }
    return static_cast<int32_t>(soonest - now);
}

void Server::fireTimers() {
    uint64_t now = nowMillis();
    // закрываем простаивающие соединения
    while (!idleList_.isEmpty()) {
        Connection *conn = ownerOf(idleList_.next, Connection, idleLink);
        if (conn->lastActiveMs + kIdleTimeoutMs >= now) {
            break;
        }
        std::fputs(std::format("removing idle connection: {}\n", conn->fd()).c_str(), stderr);
        drop(conn);
    }
    // удаляем ключи с истёкшим TTL
    db_.expireDue();
}

void Server::run() {
    std::vector<struct pollfd> watch;
    while (true) {
        watch.clear();
        watch.push_back(pollfd{.fd = listenFd_, .events = POLLIN, .revents = 0});
        for (const std::unique_ptr<Connection> &conn : connections_) {
            if (!conn) {
                continue;
            }
            short events = POLLERR;
            if (conn->wantRead)  events |= POLLIN;
            if (conn->wantWrite) events |= POLLOUT;
            watch.push_back(pollfd{.fd = conn->fd(), .events = events, .revents = 0});
        }

        int32_t timeout = nextTimeoutMs();
        int ready = poll(watch.data(), static_cast<nfds_t>(watch.size()), timeout);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            fatal("poll");
        }

        if (watch[0].revents) {
            accept();
        }

        for (size_t i = 1; i < watch.size(); ++i) {
            short got = watch[i].revents;
            if (got == 0) {
                continue;
            }
            Connection *conn = connections_[watch[i].fd].get();

            // обновляем таймер простоя: переносим соединение в хвост списка
            conn->lastActiveMs = nowMillis();
            conn->idleLink.unlink();
            idleList_.insertBefore(&conn->idleLink);

            if (got & POLLIN) {
                assert(conn->wantRead);
                conn->handleReadable(*this);
            }
            if (got & POLLOUT) {
                assert(conn->wantWrite);
                conn->handleWritable();
            }

            if ((got & POLLERR) || conn->wantClose) {
                drop(conn);
            }
        }

        fireTimers();
    }
}
