"""Scan PC Bluetooth for soldier nodes. Needs: pip install bleak

Reports devices named NU*, devices offering the Nordic UART Service (old NUS firmware),
and advertisements with company ID 0xFFFF (advertisement protocol). An 18-byte 0xFFFF payload
is a soldier packet; other lengths are unrelated devices using the test company ID.
Soldier nodes only transmit for ~300 ms per check-in (120 +/- 30 s), so scan long enough.
Usage: python ble_scan.py [seconds]
"""
import asyncio
import sys

from bleak import BleakScanner

NUS = "6e400001"


async def main(seconds: float) -> None:
    found = await BleakScanner.discover(timeout=seconds, return_adv=True)
    for addr, (dev, ad) in found.items():
        name = ad.local_name or dev.name or ""
        nus = any(NUS in u.lower() for u in ad.service_uuids)
        test_id = ad.manufacturer_data.get(0xFFFF)
        if name.upper().startswith("NU") or nus or test_id is not None:
            kind = "NUS" if nus else ""
            if test_id is not None:
                kind += f" 0xFFFF[{len(test_id)}B]" + (" SOLDIER" if len(test_id) == 18 else "")
            print(addr, repr(name), ad.rssi, kind, test_id.hex() if test_id else "")
    print("devices seen:", len(found))


if __name__ == "__main__":
    asyncio.run(main(float(sys.argv[1]) if len(sys.argv) > 1 else 20))
