#!/usr/bin/env python3
"""Collect NUS sensor data and explicitly test reconnect/subscription."""
import argparse
import asyncio
from collections import Counter
import json
import time
from bleak import BleakClient, BleakScanner
from nus_protocol import SERVICE, TX, StreamDecoder

async def run(args):
    with open(args.output, "w", encoding="utf-8") as output:
        for cycle in range(args.cycles):
            device = await BleakScanner.find_device_by_filter(
                lambda _, adv: adv.local_name == args.name and SERVICE in [s.lower() for s in adv.service_uuids], timeout=20)
            if device is None:
                raise RuntimeError(f"Device {args.name} not found")
            decoder = StreamDecoder()
            counts = Counter()
            temperatures = []
            chunks = []
            def received(_, data):
                chunks.append(len(data))
                for packet in decoder.feed(data):
                    counts[packet["type"]] += 1
                    if packet["type"] == "vitals" and packet["temp_c"] is not None:
                        temperatures.append(packet["temp_c"])
                    output.write(json.dumps(dict(packet,cycle=cycle,received_at=time.time())) + "\n")
                    output.flush()
            async with BleakClient(device) as client:
                if client.services.get_service(SERVICE) is None:
                    raise RuntimeError("NUS service absent")
                await client.start_notify(TX,received)
                print(f"cycle={cycle} connected={device.address}",flush=True)
                await asyncio.sleep(args.seconds)
                if not client.is_connected:
                    raise RuntimeError("Unexpected disconnect")
            summary = dict(cycle=cycle,counts=dict(counts),decode_errors=decoder.errors,
                           max_chunk=max(chunks,default=0),temperature_range=[min(temperatures),max(temperatures)] if temperatures else None)
            print(json.dumps(summary),flush=True)
            if decoder.errors or not counts["imu"] or not counts["status"] or max(chunks,default=0)>20:
                raise RuntimeError("NUS validation failed")
            if cycle+1 < args.cycles:
                await asyncio.sleep(2)

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name",required=True)
    parser.add_argument("--seconds",type=float,default=30)
    parser.add_argument("--cycles",type=int,default=2)
    parser.add_argument("--output",required=True)
    asyncio.run(run(parser.parse_args()))
