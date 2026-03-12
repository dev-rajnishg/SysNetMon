#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace sysnetmon {

using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;

inline int socket_error_code() {
    return WSAGetLastError();
}

inline std::string last_socket_error() {
    return "WSA error " + std::to_string(socket_error_code());
}

inline bool is_socket_valid(socket_t fd) {
    return fd != invalid_socket;
}

inline int socket_select_width(socket_t) {
    return 0;
}

inline void close_socket(socket_t fd) {
    if (is_socket_valid(fd)) {
        closesocket(fd);
    }
}

class SocketRuntime {
public:
    SocketRuntime() {
        WSADATA data {};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~SocketRuntime() {
        if (ok_) {
            WSACleanup();
        }
    }

    bool ok() const {
        return ok_;
    }

private:
    bool ok_ = false;
};

}  // namespace sysnetmon

#else

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace sysnetmon {

using socket_t = int;
constexpr socket_t invalid_socket = -1;

inline int socket_error_code() {
    return errno;
}

inline std::string last_socket_error() {
    return std::strerror(errno);
}

inline bool is_socket_valid(socket_t fd) {
    return fd >= 0;
}

inline int socket_select_width(socket_t fd) {
    return static_cast<int>(fd) + 1;
}

inline void close_socket(socket_t fd) {
    if (is_socket_valid(fd)) {
        close(fd);
    }
}

class SocketRuntime {
public:
    bool ok() const {
        return true;
    }
};

}  // namespace sysnetmon

#endif