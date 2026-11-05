// Moonlight MQTT - TLS transport over OpenSSL for Linux / Raspberry Pi / desktop.
// SPDX-License-Identifier: MIT
//
// This gives real, verified TLS on the desktop and Raspberry Pi side. On
// ESP32 / ESP8266 you do not use this file; you wrap the board's own secure
// client (WiFiClientSecure) with ArduinoTransport, which uses mbedTLS or
// BearSSL under the hood.
//
// Build with OpenSSL: link against -lssl -lcrypto.
#ifndef MOONLIGHT_TLS_TRANSPORT_H
#define MOONLIGHT_TLS_TRANSPORT_H

#if !defined(ARDUINO)

#include "Transport.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

namespace moonlight {

class TlsTransport : public Transport {
 public:
  TlsTransport() : ctx_(nullptr), ssl_(nullptr), fd_(-1), verify_(true) {}
  ~TlsTransport() override { stop(); if (ctx_) SSL_CTX_free(ctx_); }

  // Turn certificate checking on or off. ON by default. Leave it on in
  // production; turning it off makes the link insecure.
  void setVerify(bool on) { verify_ = on; }

  // Optional: trust a specific CA file (PEM). If not set, the system CA store
  // is used.
  bool setCaFile(const char* path) {
    ensureCtx();
    return SSL_CTX_load_verify_locations(ctx_, path, nullptr) == 1;
  }

  // Open a TCP socket, run the TLS handshake, and (if verify is on) check the
  // server certificate and hostname. Returns true on success.
  bool connect(const char* host, uint16_t port) {
    stop();
    ensureCtx();

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);
    struct addrinfo hints; memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host, portStr, &hints, &res) != 0) return false;

    int fd = -1;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
      fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd < 0) continue;
      if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
      ::close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return false;

    ssl_ = SSL_new(ctx_);
    if (!ssl_) { ::close(fd); return false; }
    SSL_set_fd(ssl_, fd);
    SSL_set_tlsext_host_name(ssl_, host);         // SNI
    if (verify_) {
      SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);
      X509_VERIFY_PARAM* p = SSL_get0_param(ssl_);
      X509_VERIFY_PARAM_set_hostflags(p, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      X509_VERIFY_PARAM_set1_host(p, host, 0);    // verify hostname
    }

    if (SSL_connect(ssl_) != 1) { stopFd(fd); return false; }
    if (verify_ && SSL_get_verify_result(ssl_) != X509_V_OK) { stopFd(fd); return false; }

    fd_ = fd;
    return true;
  }

  bool connected() override { return ssl_ != nullptr && fd_ >= 0; }

  int write(const uint8_t* data, size_t len) override {
    if (!ssl_) return -1;
    size_t sent = 0;
    while (sent < len) {
      int n = SSL_write(ssl_, data + sent, (int)(len - sent));
      if (n > 0) { sent += (size_t)n; continue; }
      int e = SSL_get_error(ssl_, n);
      if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
      return -1;
    }
    return (int)sent;
  }

  int read(uint8_t* data, size_t len) override {
    if (!ssl_) return -1;
    int n = SSL_read(ssl_, data, (int)len);
    if (n > 0) return n;
    int e = SSL_get_error(ssl_, n);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return 0;  // none yet
    return -1;                                                            // closed or error
  }

  void stop() override {
    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  }

 private:
  void ensureCtx() {
    if (ctx_) return;
    ctx_ = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);  // TLS 1.2+ only
    SSL_CTX_set_default_verify_paths(ctx_);               // system CA store
  }
  void stopFd(int fd) {
    if (ssl_) { SSL_free(ssl_); ssl_ = nullptr; }
    if (fd >= 0) ::close(fd);
    fd_ = -1;
  }

  SSL_CTX* ctx_;
  SSL*     ssl_;
  int      fd_;
  bool     verify_;
};

}  // namespace moonlight

#endif  // !ARDUINO
#endif  // MOONLIGHT_TLS_TRANSPORT_H
