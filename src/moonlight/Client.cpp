// moonlight - MQTT 5.0 client implementation.
// SPDX-License-Identifier: MIT
#include "Client.h"
#include "Buffer.h"

#if defined(ARDUINO)
  #include <Arduino.h>
  static inline uint32_t ml_now_ms() { return millis(); }
  static inline void     ml_sleep_ms(uint32_t ms) { delay(ms); }
#else
  #include <time.h>
  #if defined(_WIN32)
    #include <windows.h>
    static inline void ml_sleep_ms(uint32_t ms) { Sleep(ms); }
  #else
    #include <unistd.h>
    static inline void ml_sleep_ms(uint32_t ms) { usleep(ms * 1000); }
  #endif
  static inline uint32_t ml_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
  }
#endif

namespace moonlight {

static const uint8_t PROTOCOL_NAME[6] = {0x00, 0x04, 'M', 'Q', 'T', 'T'};
static const uint8_t PROTOCOL_LEVEL   = 5;
static const int16_t SUB_PENDING       = (int16_t)0x7FFF;

// Write a whole packet: fixed header (type|flags + remaining length) then body.
static int16_t writePacket(Transport& t, uint8_t firstByte,
                           const uint8_t* body, size_t bodyLen) {
  uint8_t hdr[5];
  Writer hw(hdr, sizeof(hdr));
  hw.u8(firstByte);
  hw.varint((uint32_t)bodyLen);
  if (!hw.ok()) return ERR_BUFFER_FULL;
  if (t.write(hdr, hw.length()) < 0) return ERR_TRANSPORT;
  if (bodyLen && t.write(body, bodyLen) < 0) return ERR_TRANSPORT;
  return OK;
}

// ---- IdSet (tiny fixed set of in flight QoS 2 packet ids) ----
bool Client::IdSet::has(uint16_t id) const {
  for (uint8_t i = 0; i < count; i++) if (ids[i] == id) return true;
  return false;
}
bool Client::IdSet::add(uint16_t id) {
  if (has(id)) return true;
  if (count >= MOONLIGHT_MAX_INFLIGHT) return false;
  ids[count++] = id;
  return true;
}
void Client::IdSet::remove(uint16_t id) {
  for (uint8_t i = 0; i < count; i++) {
    if (ids[i] == id) { ids[i] = ids[--count]; return; }
  }
}

// ---- Client ----
Client::Client(Transport& transport,
               uint8_t* rxBuf, size_t rxCap,
               uint8_t* txBuf, size_t txCap)
    : t_(transport), rx_(rxBuf), rxCap_(rxCap), tx_(txBuf), txCap_(txCap) {}

uint16_t Client::nextPacketId() {
  if (++packetId_ == 0) packetId_ = 1;  // id 0 is reserved
  return packetId_;
}

void Client::sendAck(uint8_t type, uint16_t packetId) {
  uint8_t body[2];
  body[0] = (uint8_t)(packetId >> 8);
  body[1] = (uint8_t)(packetId & 0xFF);
  uint8_t first = (uint8_t)(type << 4);
  if (type == PKT_PUBREL) first |= 0x02;  // PUBREL/SUBSCRIBE style flag
  writePacket(t_, first, body, 2);
}

void Client::fail(int16_t reason) {
  bool was = (state_ == CONNECTED);
  state_ = DISCONNECTED;
  stage_ = RX_TYPE;
  t_.stop();
  if (was && onDisc_) onDisc_(reason, onDiscUser_);
  // schedule the first reconnect attempt soon
  nextTry_ = ml_now_ms() + backoff_;
}

int16_t Client::sendConnect(const ConnectOptions& opts) {
  Writer w(tx_, txCap_);
  w.raw(PROTOCOL_NAME, sizeof(PROTOCOL_NAME));
  w.u8(PROTOCOL_LEVEL);

  const Will& will = opts.will;
  bool hasWill = will.topic != nullptr;

  uint8_t flags = 0;
  if (opts.cleanStart) flags |= 0x02;
  if (hasWill) {
    flags |= 0x04;
    flags |= (uint8_t)((uint8_t)will.qos << 3);
    if (will.retain) flags |= 0x20;
  }
  if (opts.password) flags |= 0x40;
  if (opts.username) flags |= 0x80;
  w.u8(flags);
  w.u16(opts.keepAlive);

  // CONNECT properties
  uint8_t props[16];
  Writer pw(props, sizeof(props));
  if (opts.sessionExpiry) { pw.u8(PROP_SESSION_EXPIRY_INTERVAL); pw.u32(opts.sessionExpiry); }
  if (opts.receiveMaximum) { pw.u8(PROP_RECEIVE_MAXIMUM); pw.u16(opts.receiveMaximum); }
  w.varint(pw.length());
  w.raw(props, pw.length());

  // payload
  w.str(opts.clientId ? opts.clientId : "");
  if (hasWill) {
    w.varint(0);                 // will properties: none
    w.str(will.topic);
    w.bytes(will.payload, will.payloadLen);
  }
  if (opts.username) w.str(opts.username);
  if (opts.password) w.str(opts.password);

  if (!w.ok()) return ERR_BUFFER_FULL;
  return writePacket(t_, (uint8_t)(PKT_CONNECT << 4), tx_, w.length());
}

int16_t Client::connect(const ConnectOptions& opts, uint32_t timeoutMs) {
  if (!t_.connected()) return ERR_TRANSPORT;

  opts_ = opts;
  haveOpts_ = true;
  keepAlive_ = opts.keepAlive;
  connackRc_ = 0xFF;
  inQos2_.count = 0;
  outQos2_.count = 0;
  stage_ = RX_TYPE;

  int16_t rc = sendConnect(opts);
  if (rc != OK) { fail(rc); return rc; }

  uint32_t start = ml_now_ms();
  while (ml_now_ms() - start < timeoutMs) {
    int16_t pr = pollOnce(50, PKT_CONNACK);
    if (pr < 0) return pr;
    if (connackRc_ != 0xFF) break;
    if (!t_.connected()) { fail(ERR_TRANSPORT); return ERR_TRANSPORT; }
  }
  if (connackRc_ == 0xFF) { fail(ERR_TIMEOUT); return ERR_TIMEOUT; }
  if (connackRc_ != 0x00) { lastReason_ = connackRc_; fail(ERR_REFUSED); return ERR_REFUSED; }

  state_    = CONNECTED;
  backoff_  = backoffMin_;
  lastPing_ = ml_now_ms();
  lastRx_   = lastPing_;
  return OK;
}

int16_t Client::tryReconnect() {
  uint32_t now = ml_now_ms();
  if (now < nextTry_) return ERR_NOT_CONNECTED;        // still in backoff
  if (!reopen_(reopenUser_) || !t_.connected()) {      // reopen the socket
    backoff_ = backoff_ * 2 > backoffMax_ ? backoffMax_ : backoff_ * 2;
    nextTry_ = now + backoff_;
    return ERR_TRANSPORT;
  }
  int16_t rc = connect(opts_, 5000);                   // redo MQTT handshake
  if (rc != OK) {
    backoff_ = backoff_ * 2 > backoffMax_ ? backoffMax_ : backoff_ * 2;
    nextTry_ = now + backoff_;
  }
  return rc;
}

int16_t Client::loop() {
  if (state_ != CONNECTED) {
    if (reopen_ && haveOpts_) return tryReconnect();
    return ERR_NOT_CONNECTED;
  }
  if (!t_.connected()) { fail(ERR_TRANSPORT); return ERR_TRANSPORT; }

  int16_t pr = pollOnce(0, 0);  // non blocking drain
  if (pr < 0) return pr;

  uint32_t now = ml_now_ms();

  // liveness: if the server has gone quiet for too long, treat as dead.
  if (keepAlive_ && (now - lastRx_) > (uint32_t)keepAlive_ * 1000U * 2U) {
    fail(ERR_TIMEOUT);
    return ERR_TIMEOUT;
  }

  // keep alive: send PINGREQ once per keepAlive interval.
  if (keepAlive_ && (now - lastPing_) >= (uint32_t)keepAlive_ * 1000U) {
    int16_t rc = writePacket(t_, (uint8_t)(PKT_PINGREQ << 4), nullptr, 0);
    if (rc != OK) { fail(rc); return rc; }
    lastPing_ = now;
  }
  return OK;
}

int16_t Client::pollOnce(uint32_t blockMs, uint8_t waitType) {
  waitType_ = waitType;
  waitHit_  = false;
  uint32_t start = ml_now_ms();

  for (;;) {
    bool progressed = false;

    if (stage_ == RX_TYPE) {
      uint8_t b;
      int n = t_.read(&b, 1);
      if (n < 0) { fail(ERR_TRANSPORT); return ERR_TRANSPORT; }
      if (n == 1) {
        rxFirst_ = b; stage_ = RX_LEN;
        rxRemaining_ = 0; lenShift_ = 0; rxFilled_ = 0;
        progressed = true;
      }
    } else if (stage_ == RX_LEN) {
      uint8_t b;
      int n = t_.read(&b, 1);
      if (n < 0) { fail(ERR_TRANSPORT); return ERR_TRANSPORT; }
      if (n == 1) {
        rxRemaining_ |= (uint32_t)(b & 0x7F) << lenShift_;
        if (b & 0x80) {
          lenShift_ += 7;
          if (lenShift_ > 21) { fail(ERR_MALFORMED); return ERR_MALFORMED; }
        } else {
          if (rxRemaining_ > rxCap_) { fail(ERR_BUFFER_FULL); return ERR_BUFFER_FULL; }
          rxFilled_ = 0;
          if (rxRemaining_ == 0) {
            dispatch(rxFirst_, rx_, 0);
            lastRx_ = ml_now_ms();
            stage_ = RX_TYPE;
            if (waitType_ && waitHit_) return OK;
          } else {
            stage_ = RX_BODY;
          }
        }
        progressed = true;
      }
    } else {  // RX_BODY
      size_t need = (size_t)rxRemaining_ - rxFilled_;
      int n = t_.read(rx_ + rxFilled_, need);
      if (n < 0) { fail(ERR_TRANSPORT); return ERR_TRANSPORT; }
      if (n > 0) {
        rxFilled_ += (size_t)n;
        progressed = true;
        if (rxFilled_ == (size_t)rxRemaining_) {
          dispatch(rxFirst_, rx_, (size_t)rxRemaining_);
          lastRx_ = ml_now_ms();
          stage_ = RX_TYPE;
          if (waitType_ && waitHit_) return OK;
        }
      }
    }

    if (!progressed) {
      if (blockMs == 0) return OK;
      if (ml_now_ms() - start >= blockMs) return OK;
      ml_sleep_ms(1);
    }
  }
}

// Skip a property block (length prefix + that many bytes).
static void skipProperties(Reader& r) {
  uint32_t plen = r.varint();
  r.skip(plen);
}

void Client::dispatch(uint8_t firstByte, const uint8_t* body, size_t bodyLen) {
  uint8_t type = firstByte >> 4;
  Reader r(body, bodyLen);

  switch (type) {
    case PKT_CONNACK: {
      r.u8();                       // acknowledge flags
      connackRc_ = r.u8();          // reason code
      uint32_t plen = r.varint();   // read server keep alive if present
      const uint8_t* pend = r.here() + plen;
      while (r.here() < pend && r.ok()) {
        uint8_t id = r.u8();
        switch (id) {
          case PROP_SERVER_KEEP_ALIVE:       keepAlive_ = r.u16(); break;
          case PROP_SESSION_EXPIRY_INTERVAL:
          case PROP_MAXIMUM_PACKET_SIZE:     r.u32(); break;
          case PROP_RECEIVE_MAXIMUM:
          case PROP_TOPIC_ALIAS_MAXIMUM:     r.u16(); break;
          case PROP_MAXIMUM_QOS:
          case PROP_RETAIN_AVAILABLE:        r.u8(); break;
          default:                           r.skip(pend - r.here()); break;
        }
      }
      if (waitType_ == PKT_CONNACK) waitHit_ = true;
      break;
    }

    case PKT_PUBLISH: {
      bool retain = firstByte & 0x01;
      QoS  qos    = (QoS)((firstByte >> 1) & 0x03);
      bool dup    = (firstByte >> 3) & 0x01;

      uint16_t topicLen = 0;
      const uint8_t* topic = r.bytes(&topicLen);
      uint16_t packetId = 0;
      if (qos > QOS0) packetId = r.u16();
      skipProperties(r);
      if (!r.ok() || !topic) break;

      bool deliver = true;
      if (qos == QOS2) {
        // Exactly once: if we have already seen this id, do not deliver again.
        deliver = !inQos2_.has(packetId);
        if (deliver) inQos2_.add(packetId);
      }

      if (deliver && onMsg_) {
        Message m;
        m.topic      = (const char*)topic;
        m.topicLen   = topicLen;
        m.payload    = r.here();
        m.payloadLen = (uint32_t)r.remaining();
        m.qos        = qos;
        m.retain     = retain;
        m.dup        = dup;
        onMsg_(m, onMsgUser_);
      }

      if (qos == QOS1) sendAck(PKT_PUBACK, packetId);
      else if (qos == QOS2) sendAck(PKT_PUBREC, packetId);
      break;
    }

    case PKT_PUBACK:                 // QoS 1 publish acknowledged
      break;
    case PKT_PUBREC: {               // QoS 2 step 2: reply PUBREL
      uint16_t id = r.u16();
      sendAck(PKT_PUBREL, id);
      break;
    }
    case PKT_PUBREL: {               // incoming QoS 2 release: reply PUBCOMP
      uint16_t id = r.u16();
      inQos2_.remove(id);
      sendAck(PKT_PUBCOMP, id);
      break;
    }
    case PKT_PUBCOMP: {              // QoS 2 complete
      uint16_t id = r.u16();
      outQos2_.remove(id);
      break;
    }

    case PKT_SUBACK: {
      r.u16();                       // packet id
      skipProperties(r);
      subRc_ = r.u8();               // first (and only) reason code
      if (waitType_ == PKT_SUBACK) waitHit_ = true;
      break;
    }
    case PKT_UNSUBACK: {
      r.u16();
      skipProperties(r);
      unsubDone_ = true;
      if (waitType_ == PKT_UNSUBACK) waitHit_ = true;
      break;
    }

    case PKT_PINGRESP:
      if (waitType_ == PKT_PINGRESP) waitHit_ = true;
      break;

    case PKT_DISCONNECT: {
      lastReason_ = bodyLen >= 1 ? body[0] : 0x00;
      fail(ERR_PROTOCOL);
      break;
    }

    default:
      break;
  }
}

int16_t Client::publish(const char* topic, const uint8_t* payload, uint32_t len,
                        QoS qos, bool retain) {
  if (state_ != CONNECTED) return ERR_NOT_CONNECTED;

  Writer w(tx_, txCap_);
  w.str(topic);
  uint16_t pid = 0;
  if (qos > QOS0) { pid = nextPacketId(); w.u16(pid); }
  w.varint(0);                 // no properties
  w.raw(payload, len);
  if (!w.ok()) return ERR_BUFFER_FULL;

  if (qos == QOS2 && !outQos2_.add(pid)) return ERR_BUFFER_FULL;  // too many in flight

  uint8_t first = (uint8_t)(PKT_PUBLISH << 4) | ((uint8_t)qos << 1) | (retain ? 0x01 : 0x00);
  int16_t rc = writePacket(t_, first, tx_, w.length());
  if (rc != OK) { if (qos == QOS2) outQos2_.remove(pid); fail(rc); }
  return rc;
}

int16_t Client::publish(const char* topic, const char* payload, QoS qos, bool retain) {
  return publish(topic, (const uint8_t*)payload, payload ? (uint32_t)strlen(payload) : 0, qos, retain);
}

int16_t Client::subscribe(const char* topicFilter, QoS maxQos, uint32_t timeoutMs) {
  if (state_ != CONNECTED) return ERR_NOT_CONNECTED;

  Writer w(tx_, txCap_);
  w.u16(nextPacketId());
  w.varint(0);                 // no properties
  w.str(topicFilter);
  w.u8((uint8_t)maxQos);       // subscription options: QoS in bits 0-1
  if (!w.ok()) return ERR_BUFFER_FULL;

  subRc_ = SUB_PENDING;
  int16_t rc = writePacket(t_, (uint8_t)((PKT_SUBSCRIBE << 4) | 0x02), tx_, w.length());
  if (rc != OK) { fail(rc); return rc; }

  uint32_t start = ml_now_ms();
  while (subRc_ == SUB_PENDING && ml_now_ms() - start < timeoutMs) {
    int16_t pr = pollOnce(50, PKT_SUBACK);
    if (pr < 0) return pr;
  }
  if (subRc_ == SUB_PENDING) return ERR_TIMEOUT;
  if (subRc_ >= 0x80) { lastReason_ = (uint8_t)subRc_; return ERR_REFUSED; }
  return subRc_;               // granted QoS (0/1/2)
}

int16_t Client::unsubscribe(const char* topicFilter, uint32_t timeoutMs) {
  if (state_ != CONNECTED) return ERR_NOT_CONNECTED;

  Writer w(tx_, txCap_);
  w.u16(nextPacketId());
  w.varint(0);
  w.str(topicFilter);
  if (!w.ok()) return ERR_BUFFER_FULL;

  unsubDone_ = false;
  int16_t rc = writePacket(t_, (uint8_t)((PKT_UNSUBSCRIBE << 4) | 0x02), tx_, w.length());
  if (rc != OK) { fail(rc); return rc; }

  uint32_t start = ml_now_ms();
  while (!unsubDone_ && ml_now_ms() - start < timeoutMs) {
    int16_t pr = pollOnce(50, PKT_UNSUBACK);
    if (pr < 0) return pr;
  }
  return unsubDone_ ? OK : ERR_TIMEOUT;
}

void Client::disconnect(uint8_t reasonCode) {
  reopen_ = nullptr;             // stop auto reconnect
  if (t_.connected()) {
    if (reasonCode == 0x00) {
      writePacket(t_, (uint8_t)(PKT_DISCONNECT << 4), nullptr, 0);
    } else {
      uint8_t bodyb[2] = { reasonCode, 0x00 };  // reason code + no properties
      writePacket(t_, (uint8_t)(PKT_DISCONNECT << 4), bodyb, 2);
    }
  }
  state_ = DISCONNECTED;
  stage_ = RX_TYPE;
  t_.stop();
}

}  // namespace moonlight
