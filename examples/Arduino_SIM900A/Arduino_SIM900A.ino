// Moonlight MQTT - Arduino with a SIM900A GSM module (plain MQTT).
//
// Runs on a normal Arduino (Uno, Nano, Mega) using the TinyGSM library to drive
// a SIM900A 2G module. Moonlight talks to a transport, so the TinyGsmClient
// plugs in just like a WiFi client does.
//
// Note: SIM900A is a 2G module and does not support TLS, so this example is
// plain MQTT, not secure. Small Arduino boards also do not have the memory for
// TLS. For a secure link, use the ESP32 example instead.
//
// Wiring (change pins for your board):
//   Arduino pin 7  <- SIM900A TX
//   Arduino pin 8  -> SIM900A RX
//   Common ground, and a power supply that can give the module enough current.
//
// Needs the TinyGSM library (install it from the Library Manager).

#define TINY_GSM_MODEM_SIM900
#include <SoftwareSerial.h>
#include <TinyGsmClient.h>
#include <MoonlightMQTT.h>

using namespace moonlight;

// Your network and broker details.
const char* APN       = "your-apn";
const char* GPRS_USER = "";
const char* GPRS_PASS = "";
const char* BROKER    = "your-broker.example.com";
const uint16_t PORT   = 1883;

// Serial link to the SIM900A. SoftwareSerial pins: RX = 7, TX = 8.
// On a Mega you can use a hardware port (Serial1) instead and remove this.
SoftwareSerial SerialAT(7, 8);

TinyGsm        modem(SerialAT);
TinyGsmClient  net(modem);
ArduinoTransport<TinyGsmClient> transport(net);

// Small buffers to fit limited Arduino memory.
StaticClient<512, 256> mqtt(transport);

bool reopen(void*) {
  if (!modem.isGprsConnected()) {
    if (!modem.gprsConnect(APN, GPRS_USER, GPRS_PASS)) return false;
  }
  return net.connect(BROKER, PORT) == 1;
}

void onMessage(const Message& m, void*) {
  Serial.print("message on ");
  Serial.write((const uint8_t*)m.topic, m.topicLen);
  Serial.print(": ");
  Serial.write(m.payload, m.payloadLen);
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  SerialAT.begin(9600);     // SIM900A with SoftwareSerial works best at 9600
  delay(3000);

  Serial.println("starting modem...");
  modem.restart();
  if (!modem.waitForNetwork()) { Serial.println("no network"); return; }
  if (!modem.gprsConnect(APN, GPRS_USER, GPRS_PASS)) { Serial.println("gprs failed"); return; }
  Serial.println("cellular connected");

  if (!net.connect(BROKER, PORT)) { Serial.println("TCP connect failed"); return; }

  mqtt.onMessage(onMessage);
  mqtt.enableAutoReconnect(reopen, nullptr);

  ConnectOptions opts;
  opts.clientId   = "moonlight-sim900a";
  opts.keepAlive  = 60;
  opts.cleanStart = true;

  if (mqtt.connect(opts) != OK) { Serial.println("MQTT connect failed"); return; }
  Serial.println("MQTT connected");

  mqtt.subscribe("moonlight/arduino/cmd", QOS1);
}

unsigned long lastPublish = 0;

void loop() {
  mqtt.loop();
  if (mqtt.connected() && millis() - lastPublish > 10000) {
    lastPublish = millis();
    mqtt.publish("moonlight/arduino/hello", "hi from sim900a", QOS1);
  }
}
