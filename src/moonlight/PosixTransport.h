// moonlight - POSIX TCP transport for Linux / Raspberry Pi / macOS.
// SPDX-License-Identifier: MIT
#ifndef MOONLIGHT_POSIX_TRANSPORT_H
#define MOONLIGHT_POSIX_TRANSPORT_H

#if !defined(ARDUINO)

#include "Transport.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace moonlight {

class PosixTransport : public Transport {
 public:
  PosixTransport() : fd_(-1) {}
  ~PosixTransport() override { stop(); }

  // Open a blocking connect TCP socket, then switch it to non blocking so
  // read() can poll. Returns true on success.
  bool connect(const char* host, uint16_t port) {
    stop();
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    if (getaddrinfo(host, portStr, &hints, &res) != 0) return false;

    int fd = -1;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
      fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd < 0) continue;
      if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
      ::close(fd);
      fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return false;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    fd_ = fd;
    return true;
  }

  bool connected() override { return fd_ >= 0; }

  int write(const uint8_t* data, size_t len) override {
    if (fd_ < 0) return -1;
    size_t sent = 0;
    while (sent < len) {
      ssize_t n = ::send(fd_, data + sent, len - sent, MSG_NOSIGNAL);
      if (n > 0) { sent += (size_t)n; continue; }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
      return -1;  // peer closed or hard error
    }
    return (int)sent;
  }

  int read(uint8_t* data, size_t len) override {
    if (fd_ < 0) return -1;
    ssize_t n = ::recv(fd_, data, len, 0);
    if (n > 0) return (int)n;
    if (n == 0) return -1;  // orderly shutdown by peer
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
  }

  void stop() override {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  }

 private:
  int fd_;
};

}  // namespace moonlight

#endif  // !ARDUINO
#endif  // MOONLIGHT_POSIX_TRANSPORT_H
