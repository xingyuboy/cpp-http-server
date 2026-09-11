#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace hs {

#ifdef _WIN32
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

class SocketError : public std::runtime_error {
public:
    SocketError(const std::string& what, int code)
        : std::runtime_error(what), code_(code) {}
    int code() const noexcept { return code_; }

private:
    int code_;
};

int lastSocketError();
std::string socketErrorMessage(int code);

enum class IoStatus { Ok, Closed, Timeout, Error };

struct IoResult {
    IoStatus status = IoStatus::Ok;
    std::size_t bytes = 0;
};

// Move-only owner of a connected socket. Closing is done by the destructor so
// that an exception anywhere in request handling still releases the handle.
class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(SocketHandle handle) noexcept : handle_(handle) {}
    ~TcpSocket() { close(); }

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    bool valid() const noexcept { return handle_ != kInvalidSocket; }
    SocketHandle handle() const noexcept { return handle_; }

    IoResult receive(char* buffer, std::size_t length);
    IoResult sendAll(const char* data, std::size_t length);
    IoResult sendAll(const std::string& data) { return sendAll(data.data(), data.size()); }

    void setReceiveTimeout(int milliseconds);
    void setSendTimeout(int milliseconds);
    void setNoDelay(bool enabled);

    // Half-close: tells the peer we are done writing without discarding data
    // still in flight, which avoids RST on the last response of a connection.
    void shutdownWrite();
    void close() noexcept;

private:
    SocketHandle handle_ = kInvalidSocket;
};

class TcpListener {
public:
    void bindAndListen(const std::string& address, std::uint16_t port, int backlog);

    // Polls instead of blocking forever so the accept loop can observe the
    // shutdown flag without needing to be woken by a dummy connection.
    std::optional<TcpSocket> acceptWithTimeout(int timeoutMs, std::string& peerAddress);

    std::uint16_t localPort() const;
    bool isOpen() const noexcept { return socket_.valid(); }
    void close() noexcept { socket_.close(); }

private:
    TcpSocket socket_;
};

TcpSocket connectTcp(const std::string& host, std::uint16_t port, int timeoutMs = 5000);

}  // namespace hs
