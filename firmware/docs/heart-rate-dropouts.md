# Heart-rate dropout handling

A missed beat previously lengthened the detector refractory period. A synthetic
60 BPM signal with one omitted beat reproduced subsequent detection at 30 BPM.
The refractory period now uses the shorter of the latest interval and the
confirmed cadence. Three consistent intervals (within 25% of the candidate
median) are required before publishing an updated rate. A five-interval median
limits the effect of individual timing artifacts. An inconsistent interval
starts reacquisition; it does not enter the confirmed rate average.

Clipping and sampling gaps still invalidate the measurement. Confirmed values
expire after 2.5 seconds without a confirmed update. Firmware sends zero for
unavailable heart rate, never a fabricated replacement. The wire format remains
18 bytes. Unknown heart rate disables heart-rate-dependent node alert rules.

The tactical-c2 compatibility branch adds display-only continuity:

- `hr_bpm`: current received measurement, or null when unavailable/stale.
- `hr_display_bpm`: current value or a bounded last received value.
- `hr_status`: `live`, `reacquiring`, or `unavailable`.
- `hr_age_s`: seconds since the last received nonzero heart-rate value.

After unavailable packets arrive, the UI can retain the last number until eight
seconds after the last nonzero packet, explicitly labeled as a last value while
reacquiring. Unavailable packets do not refresh that deadline. Packet loss beyond
the radio stale deadline cancels the display hold. Held values never enter PSI,
baseline acquisition, or alert assessment. Eight seconds is a UI grace period,
not a physiological threshold. Sensor unavailability does not establish death.
Firmware already bounds its own last confirmed rate to 2.5 seconds, so receipt
age is not an exact timestamp of the last optical beat.

HR-dependent alert packets without HR are ignored by the compatibility backend;
manual SOS and fall alerts remain eligible. Existing genuine active alerts are
not silently cleared when a sensor loses contact.

Validation covers missed beats, irregular intervals, clean waveforms, electrical
interference, recorded no-contact noise, clipping, timestamp wrap, display expiry,
recovery, and missing-HR alerts. This does not establish clinical accuracy or
prove stable optical contact on the physical sensor. The commander must run the
updated backend and frontend together to show the new status fields.

## Amplitude recovery

A separate regression uses clean 60 BPM pulses with raw amplitude alternating
between 800 and 80 ADC counts every ten seconds. The previous detector lost its
rate despite the continuing pulses. During the latter part of an expected beat
interval, the threshold and peak/trough envelope now relax toward the local
filtered signal with a 150 ms time constant. This follows a changing baseline
and forgets old large peaks without reducing the minimum four-count swing gate.
The regression maintains 58-62 BPM through both amplitude transitions. Existing
no-contact recordings, mains interference, clipping, and cadence tests still
pass. A synthetic regression does not establish the physical cause of every
reported dropout.

An optional `CFG_PULSE_TRACE=1` diagnostic freezes a 2048-sample rolling raw ADC
buffer when published BPM first changes from nonzero to unavailable. It reports
`PTRACE_TIME` and indexed `PTRACE` hex rows after acquisition stops. At 500 Hz,
the buffer spans about four seconds. Missing log rows must be detected before
replaying the trace; timestamps bound the window but do not encode sample jitter.
The production build default remains trace-disabled. Diagnostic output finishes
after one capture and does not repeatedly flood the serial link.
