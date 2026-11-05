// moonlight - transport abstraction.
// SPDX-License-Identifier: MIT
//
// The client talks to the network only through this interface, so the same
// code runs over an Arduino WiFiClient/EthernetClient, a POSIX socket, or a
// test double. Implement these four methods for any new transport.
#ifndef MOONLIGHT_TRANSPORT_H
#define MOONLIGHT_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

namespace moonlight {

class Transport {
 public:
  virtual ~Transport() {}

  // True once the underlying connection is open and usable.
  virtual bool connected() = 0;

  // Write exactly len bytes. Return bytes written (len on success) or <0.
  virtual int write(const uint8_t* data, size_t len) = 0;

  // Read up to len bytes without blocking longer than needed. Return the
  // number read (0 if none available right now) or <0 on error.
  virtual int read(uint8_t* data, size_t len) = 0;

  // Close the connection.
  virtual void stop() = 0;
};

// Adapter for any class with the Arduino Client API (WiFiClient, EthernetClient,
// WiFiClientSecure, ...). Open the socket yourself (client.connect(host, port))
// before handing it to moonlight::Client.
#if defined(ARDUINO)
template <typename ArduinoClient>
class ArduinoTransport : public Transport {
 public:
  explicit ArduinoTransport(ArduinoClient& c) : c_(c) {}
  bool connected() override { return c_.connected(); }
  int write(const uint8_t* data, size_t len) override { return (int)c_.write(data, len); }
  int read(uint8_t* data, size_t len) override {
    int avail = c_.available();
    if (avail <= 0) return 0;
    size_t n = (size_t)avail < len ? (size_t)avail : len;
    return c_.read(data, n);
  }
  void stop() override { c_.stop(); }
 private:
  ArduinoClient& c_;
};
#endif  // ARDUINO

}  // namespace moonlight

#endif  // MOONLIGHT_TRANSPORT_H
