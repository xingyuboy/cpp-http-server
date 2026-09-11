#include "net/Socket.hpp"

#include <cstring>
#include <system_error>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace hs {
namespace {

void closeHandle(SocketHandle handle) {
#ifdef _WIN32
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

bool errorIsTimeout(int code) {
#ifdef _WIN32
    return code == WSAETIMEDOUT || code == WSAEWOULDBLOCK;
#else
    return code == EAGAIN || code == EWOULDBLOCK;
#endif
}

bool errorIsInterrupt(int code) {
#ifdef _WIN32
    return code == WSAEINTR;
#else
    return code == EINTR;
#endif
}

bool errorIsDisconnect(int code) {
#ifdef _WIN32
    return code == WSAECONNRESET || code == WSAECONNABORTED || code == WSAENETRESET;
#else
    return code == ECONNRESET || code == EPIPE;
#endif
}

#ifdef _WIN32
struct WinsockGuard {
    WinsockGuard() {
        WSADATA data{};
        const int rc = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (rc != 0) {
            throw SocketError("WSAStartup failed: " + socketErrorMessage(rc), rc);
        }
    }
    ~WinsockGuard() { ::WSACleanup(); }
};
#endif

// Called before creating any socket rather than left to main(), so no entry
// point can forget it. Winsock reference-counts WSAStartup, so this is safe
// even if something else has already initialised it.
void ensureNetworkReady() {
#ifdef _WIN32
    static WinsockGuard guard;
#endif
}

std::string describeEndpoint(const sockaddr_storage& addr) {
    char host[NI_MAXHOST] = {};
    char service[NI_MAXSERV] = {};
    const int rc = ::getnameinfo(reinterpret_cast<const sockaddr*>(&addr), sizeof(addr), host,
                                 sizeof(host), service, sizeof(service),
                                 NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
        return "unknown";
    }
    return std::string(host) + ":" + service;
}

}  // namespace


int lastSocketError() {
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

std::string socketErrorMessage(int code) {
#ifdef _WIN32
    return std::system_category().message(code);
#else
    return std::generic_category().message(code);
#endif
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = kInvalidSocket;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = kInvalidSocket;
    }
    return *this;
}

void TcpSocket::close() noexcept {
    if (handle_ != kInvalidSocket) {
        closeHandle(handle_);
        handle_ = kInvalidSocket;
    }
}

void TcpSocket::shutdownWrite() {
    if (handle_ == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::shutdown(handle_, SD_SEND);
#else
    ::shutdown(handle_, SHUT_WR);
#endif
}

IoResult TcpSocket::receive(char* buffer, std::size_t length) {
    while (true) {
#ifdef _WIN32
        const int n = ::recv(handle_, buffer, static_cast<int>(length), 0);
#else
        const auto n = ::recv(handle_, buffer, length, 0);
#endif
        if (n > 0) {
            return {IoStatus::Ok, static_cast<std::size_t>(n)};
        }
        if (n == 0) {
            return {IoStatus::Closed, 0};
        }
        const int code = lastSocketError();
        if (errorIsInterrupt(code)) {
            continue;
        }
        if (errorIsTimeout(code)) {
            return {IoStatus::Timeout, 0};
        }
        if (errorIsDisconnect(code)) {
            return {IoStatus::Closed, 0};
        }
        return {IoStatus::Error, 0};
    }
}

IoResult TcpSocket::sendAll(const char* data, std::size_t length) {
    std::size_t sent = 0;
    while (sent < length) {
#ifdef _WIN32
        const int n = ::send(handle_, data + sent, static_cast<int>(length - sent), 0);
#else
        // MSG_NOSIGNAL keeps a write to a closed peer from killing the process
        // with SIGPIPE; on Windows the equivalent simply returns an error.
        const auto n = ::send(handle_, data + sent, length - sent, MSG_NOSIGNAL);
#endif
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        const int code = lastSocketError();
        if (errorIsInterrupt(code)) {
            continue;
        }
        if (errorIsTimeout(code)) {
            return {IoStatus::Timeout, sent};
        }
        if (errorIsDisconnect(code) || n == 0) {
            return {IoStatus::Closed, sent};
        }
        return {IoStatus::Error, sent};
    }
    return {IoStatus::Ok, sent};
}

void TcpSocket::setReceiveTimeout(int milliseconds) {
#ifdef _WIN32
    DWORD value = static_cast<DWORD>(milliseconds);
    ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value),
                 sizeof(value));
#else
    timeval value{};
    value.tv_sec = milliseconds / 1000;
    value.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
#endif
}

void TcpSocket::setSendTimeout(int milliseconds) {
#ifdef _WIN32
    DWORD value = static_cast<DWORD>(milliseconds);
    ::setsockopt(handle_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value),
                 sizeof(value));
#else
    timeval value{};
    value.tv_sec = milliseconds / 1000;
    value.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(handle_, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value));
#endif
}

void TcpSocket::setNoDelay(bool enabled) {
    const int value = enabled ? 1 : 0;
    ::setsockopt(handle_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&value),
                 sizeof(value));
}

void TcpListener::bindAndListen(const std::string& address, std::uint16_t port, int backlog) {
    ensureNetworkReady();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int rc = ::getaddrinfo(address.empty() ? nullptr : address.c_str(), portText.c_str(),
                                 &hints, &results);
    if (rc != 0) {
        throw SocketError("cannot resolve " + address + ": " + socketErrorMessage(rc), rc);
    }

    SocketHandle handle = ::socket(results->ai_family, results->ai_socktype, results->ai_protocol);
    if (handle == kInvalidSocket) {
        const int code = lastSocketError();
        ::freeaddrinfo(results);
        throw SocketError("socket() failed: " + socketErrorMessage(code), code);
    }

#ifndef _WIN32
    // On Windows SO_REUSEADDR lets an unrelated process steal the port, so it is
    // deliberately only enabled on POSIX where it just skips TIME_WAIT.
    const int reuse = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    if (::bind(handle, results->ai_addr, static_cast<int>(results->ai_addrlen)) != 0) {
        const int code = lastSocketError();
        ::freeaddrinfo(results);
        closeHandle(handle);
        throw SocketError("bind to " + address + ":" + portText + " failed: " +
                              socketErrorMessage(code),
                          code);
    }
    ::freeaddrinfo(results);

    if (::listen(handle, backlog) != 0) {
        const int code = lastSocketError();
        closeHandle(handle);
        throw SocketError("listen() failed: " + socketErrorMessage(code), code);
    }

    socket_ = TcpSocket(handle);
}

std::optional<TcpSocket> TcpListener::acceptWithTimeout(int timeoutMs, std::string& peerAddress) {
    if (!socket_.valid()) {
        return std::nullopt;
    }

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket_.handle(), &readable);

    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
    // Winsock ignores the first argument, and SOCKET is a pointer-sized handle
    // that must not be squeezed into an int.
    const int ready = ::select(0, &readable, nullptr, nullptr, &timeout);
#else
    const int ready = ::select(socket_.handle() + 1, &readable, nullptr, nullptr, &timeout);
#endif
    if (ready <= 0) {
        return std::nullopt;
    }

    sockaddr_storage peer{};
#ifdef _WIN32
    int peerLength = sizeof(peer);
#else
    socklen_t peerLength = sizeof(peer);
#endif
    SocketHandle client = ::accept(socket_.handle(), reinterpret_cast<sockaddr*>(&peer),
                                   &peerLength);
    if (client == kInvalidSocket) {
        return std::nullopt;
    }

    peerAddress = describeEndpoint(peer);
    return TcpSocket(client);
}

std::uint16_t TcpListener::localPort() const {
    sockaddr_storage addr{};
#ifdef _WIN32
    int length = sizeof(addr);
#else
    socklen_t length = sizeof(addr);
#endif
    if (::getsockname(socket_.handle(), reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
        return 0;
    }
    return ntohs(reinterpret_cast<const sockaddr_in*>(&addr)->sin_port);
}

TcpSocket connectTcp(const std::string& host, std::uint16_t port, int timeoutMs) {
    ensureNetworkReady();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int rc = ::getaddrinfo(host.c_str(), portText.c_str(), &hints, &results);
    if (rc != 0) {
        throw SocketError("cannot resolve " + host + ": " + socketErrorMessage(rc), rc);
    }

    SocketHandle handle = ::socket(results->ai_family, results->ai_socktype, results->ai_protocol);
    if (handle == kInvalidSocket) {
        const int code = lastSocketError();
        ::freeaddrinfo(results);
        throw SocketError("socket() failed: " + socketErrorMessage(code), code);
    }

    const int connectResult = ::connect(handle, results->ai_addr,
                                        static_cast<int>(results->ai_addrlen));
    ::freeaddrinfo(results);
    if (connectResult != 0) {
        const int code = lastSocketError();
        closeHandle(handle);
        throw SocketError("connect to " + host + ":" + portText + " failed: " +
                              socketErrorMessage(code),
                          code);
    }

    TcpSocket socket(handle);
    socket.setReceiveTimeout(timeoutMs);
    socket.setSendTimeout(timeoutMs);
    return socket;
}

}  // namespace hs
