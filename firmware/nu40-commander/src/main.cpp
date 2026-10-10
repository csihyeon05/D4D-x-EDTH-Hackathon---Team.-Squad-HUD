#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>
#include <queue.h>
#include <atomic>
#include "../gateway_core.h"

namespace {
struct Received { uint8_t packet[gw::PACKET_SIZE]; int8_t rssi; };
QueueHandle_t packets;
gw::Deduper deduper;
std::atomic<uint32_t> adverts{0}, drops{0};
uint32_t forwarded=0, duplicates=0, flashUntil=0;

// Copy reports before resuming; the SoftDevice owns the report buffer.
void onScan(ble_gap_evt_adv_report_t* report) {
  ++adverts;
  Received item;
  if (gw::extractPacket(report->data.p_data, report->data.len, item.packet)) {
    item.rssi=report->rssi;
    if (xQueueSend(packets, &item, 0)!=pdPASS) ++drops;
  }
  Bluefruit.Scanner.resume();
}
void fail(const char* message) {
  Serial.println(message);
  for (;;) { digitalWrite(LED_BUILTIN, LED_STATE_ON); delay(100);
    digitalWrite(LED_BUILTIN, !LED_STATE_ON); delay(100); }
}
}
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  packets=xQueueCreate(32,sizeof(Received));
  if (!packets) fail("GW,error=queue");
  if (!Bluefruit.begin(0,1)) fail("GW,error=ble");
  Bluefruit.autoConnLed(false);
  Bluefruit.setName("NU40-Gateway");
  Bluefruit.Scanner.setRxCallback(onScan);
  Bluefruit.Scanner.useActiveScan(false);
  // Units are 0.625 ms. A full window receives short soldier bursts.
  Bluefruit.Scanner.setInterval(160,160);
  if (!Bluefruit.Scanner.start(0)) fail("GW,error=scan");
}
void loop() {
  const uint32_t now=millis();
  Received item;
  // Avoid blocking the BLE task on an absent or slow USB host.
  while (Serial && Serial.availableForWrite()>=int(gw::LINE_SIZE+2)
         && xQueueReceive(packets,&item,0)==pdPASS) {
    if (!deduper.accept(gw::soldierId(item.packet),gw::sequence(item.packet),now)) {
      ++duplicates; continue;
    }
    char line[gw::LINE_SIZE];
    if (gw::formatLine(line,sizeof(line),item.packet,item.rssi)>0) {
      Serial.println(line); ++forwarded; flashUntil=now+75;
    }
  }
  static uint32_t lastStats=0;
  if (now-lastStats>=10000 && Serial && Serial.availableForWrite()>=100) {
    lastStats=now;
    Serial.printf("GW,uptime_s=%lu,adverts=%lu,forwarded=%lu,duplicates=%lu,queue_drops=%lu\n",
      (unsigned long)(now/1000),(unsigned long)adverts.load(),
      (unsigned long)forwarded,(unsigned long)duplicates,(unsigned long)drops.load());
  }
  const bool lit=int32_t(flashUntil-now)>0 || now%2000<40;
  digitalWrite(LED_BUILTIN,lit ? LED_STATE_ON : !LED_STATE_ON);
  delay(2);
}
