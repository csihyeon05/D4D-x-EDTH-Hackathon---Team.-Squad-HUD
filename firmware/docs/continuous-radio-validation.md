# Continuous radio and pulse diagnostics validation

## Changes

The soldier remains a non-connectable manufacturer-data broadcaster. It now
updates only GAP_PARAM_ADV_DATA through le_adv_update_param and observes the
asynchronous GAP_MSG_LE_ADV_UPDATE_PARAM response. It no longer stops and starts
advertising for each measurement. Initial advertising has the complete 25-byte
layout, with unavailable sensor values. Advertising uses a 100-120 ms interval;
the latest display measurement is still refreshed every second.

TX_QUEUE means queued, not transmitted. RADIO separately reports GAP state,
queued count, accepted requests, completed callbacks, failures and timeouts.
Controller completion is not proof of RF transmission or receiver delivery.
The application does not overlap update requests. A missing callback is exposed
after two seconds; a late callback is still accepted. It does not reset the MCU
or silently claim recovery from a permanently stuck controller.

## Physical validation

An initial continuous-update experiment with a flags-only startup payload and
20-30 ms interval received 183 successful update callbacks but no sensor packets
in the PC scan. The final radio configuration changes both startup payload and
interval. Their individual contributions were not isolated, and the original
start/stop stall hypothesis has not been proven as the sole root cause.

With the final radio configuration, a 180-second PC scan received 158 distinct
packets, including startup, through node second 181. Median interval between
unique receptions was 1.050 s; maximum was 3.030 s. At node second 184 the serial
log showed gap=2, q=184, req=183, done=183, fail=0, timeout=0, start=1.
IMU read_errors=0 and starts=1. See validation/continuous-radio.csv.
Advertising stayed active beyond the reported initial few successful packets.
This is a local PC radio test, not an independent physical NU40 test, a lossless
transport guarantee or a long-duration endurance test.

## Pulse findings and limits

Before filtering, finger contact produced both plausible 72-85 BPM values and
unstable 230+ BPM values, despite the user reporting steady contact. After the
user removed their finger, the original detector also briefly reported 25-40
BPM from ADC noise. Contact accuracy therefore was not established.

A 1024-sample no-contact recording spanned 2046 ms (500 Hz) and raw values
2032-2065. It is stored as test/fixtures/pulse-no-contact.txt in each soldier
project. Tests replay this measured trace and add synthetic 50/60/100/120 Hz
interference. Two approximately 5 Hz low-pass stages and slow DC removal now
precede beat detection. Tests preserve 40/60/90/120/180 BPM signals, clipping,
stale-sample handling and timestamp wrap. The recorded noise produces no BPM.

PULSE logs full-window raw minimum/maximum, count, clipping and BPM every second.
The optional trace waits 30 seconds after boot and is disabled in the final
normal build. Clipped or absent optical signals remain unavailable. Filtering
cannot establish sensor placement or medical accuracy. Post-filter physical
contact validation remains unverified; the user was told to remove their finger
and no further prolonged contact was required.

Both real target builds and both native suites passed, including protocol parity
against tactical-c2 and the new asynchronous advertiser API tests.

## Final uploaded build, no-contact check

The final build was uploaded with trace disabled. A 60-second scan received
56 distinct packets; every BPM field was unavailable, matching all
63 one-second PULSE windows (BPM 0). Median reception interval was
1.039 s and maximum gap 2.930 s. Final radio counters:

```text
RADIO,gap=2,q=64,req=63,done=63,fail=0,timeout=0,start=1,wait=0,cause=0,age=982
```

The same received bytes are replayed through the tactical-c2 serial reader
for REST/WebSocket handoff; this does not substitute for a physical NU40 test.
