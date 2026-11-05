// moonlight - buffer writer/reader with MQTT wire format helpers.
// SPDX-License-Identifier: MIT
#ifndef MOONLIGHT_BUFFER_H
#define MOONLIGHT_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace moonlight {

// Writer appends MQTT wire format fields into a caller owned fixed buffer.
// Every append is bounds checked; once a write overflows, the Writer stays in
// an "overflowed" state and all later writes are ignored. Callers check ok()
// once at the end instead of after every field.
class Writer {
 public:
  Writer(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap), len_(0), ok_(true) {}

  bool   ok() const { return ok_; }
  size_t length() const { return len_; }
  uint8_t* data() const { return buf_; }

  void u8(uint8_t v) {
    if (!reserve(1)) return;
    buf_[len_++] = v;
  }

  void u16(uint16_t v) {  // big endian
    if (!reserve(2)) return;
    buf_[len_++] = (uint8_t)(v >> 8);
    buf_[len_++] = (uint8_t)(v & 0xFF);
  }

  void u32(uint32_t v) {  // big endian
    if (!reserve(4)) return;
    buf_[len_++] = (uint8_t)(v >> 24);
    buf_[len_++] = (uint8_t)(v >> 16);
    buf_[len_++] = (uint8_t)(v >> 8);
    buf_[len_++] = (uint8_t)(v & 0xFF);
  }

  void raw(const uint8_t* p, size_t n) {
    if (!reserve(n)) return;
    memcpy(buf_ + len_, p, n);
    len_ += n;
  }

  // UTF-8 string / binary data: 2-byte length prefix then the bytes.
  void str(const char* s) { bytes((const uint8_t*)s, s ? strlen(s) : 0); }
  void bytes(const uint8_t* p, size_t n) {
    u16((uint16_t)n);
    raw(p, n);
  }

  // Variable Byte Integer (1-4 bytes), used for lengths and property ids.
  void varint(uint32_t v) {
    do {
      uint8_t b = v & 0x7F;
      v >>= 7;
      if (v) b |= 0x80;
      u8(b);
    } while (v);
  }

  // Overwrite 1 byte at an earlier position (used to backfill header fields).
  void patchU8(size_t pos, uint8_t v) {
    if (pos < len_) buf_[pos] = v;
  }

 private:
  bool reserve(size_t n) {
    if (!ok_ || len_ + n > cap_) { ok_ = false; return false; }
    return true;
  }
  uint8_t* buf_;
  size_t   cap_;
  size_t   len_;
  bool     ok_;
};

// Reader parses MQTT wire format fields from a buffer. On any out of range
// read it flips ok() to false and returns zero/empty values.
class Reader {
 public:
  Reader(const uint8_t* buf, size_t len) : buf_(buf), len_(len), pos_(0), ok_(true) {}

  bool   ok() const { return ok_; }
  size_t remaining() const { return pos_ <= len_ ? len_ - pos_ : 0; }
  const uint8_t* here() const { return buf_ + pos_; }
  void   skip(size_t n) { if (!avail(n)) return; pos_ += n; }

  uint8_t u8() {
    if (!avail(1)) return 0;
    return buf_[pos_++];
  }
  uint16_t u16() {
    if (!avail(2)) return 0;
    uint16_t v = ((uint16_t)buf_[pos_] << 8) | buf_[pos_ + 1];
    pos_ += 2;
    return v;
  }
  uint32_t u32() {
    if (!avail(4)) return 0;
    uint32_t v = ((uint32_t)buf_[pos_] << 24) | ((uint32_t)buf_[pos_ + 1] << 16) |
                 ((uint32_t)buf_[pos_ + 2] << 8) | buf_[pos_ + 3];
    pos_ += 4;
    return v;
  }

  // Returns a pointer into the buffer (not copied) and sets outLen.
  const uint8_t* bytes(uint16_t* outLen) {
    uint16_t n = u16();
    if (!avail(n)) { *outLen = 0; return nullptr; }
    const uint8_t* p = buf_ + pos_;
    pos_ += n;
    *outLen = n;
    return p;
  }

  uint32_t varint() {
    uint32_t v = 0;
    uint8_t shift = 0;
    for (int i = 0; i < 4; i++) {
      if (!avail(1)) return 0;
      uint8_t b = buf_[pos_++];
      v |= (uint32_t)(b & 0x7F) << shift;
      if (!(b & 0x80)) return v;
      shift += 7;
    }
    ok_ = false;  // 5th continuation byte is illegal
    return 0;
  }

 private:
  bool avail(size_t n) {
    if (!ok_ || pos_ + n > len_) { ok_ = false; return false; }
    return true;
  }
  const uint8_t* buf_;
  size_t         len_;
  size_t         pos_;
  bool           ok_;
};

}  // namespace moonlight

#endif  // MOONLIGHT_BUFFER_H
