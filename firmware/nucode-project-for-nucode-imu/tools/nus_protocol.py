"""NUS newline-delimited sensor telemetry v1; one decoder per connection."""
import json
import math
SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

def integer(p, key, low, high):
    v = p[key]
    if type(v) is not int or not low <= v <= high:
        raise ValueError(f"Invalid {key}")
    return v

def vector(p, key, count, low, high):
    v = p[key]
    if not isinstance(v, list) or len(v) != count:
        raise ValueError(f"Invalid {key}")
    for x in v:
        integer({key: x}, key, low, high)
    return v

def decode(line):
    p = json.loads(line)
    if not isinstance(p, dict) or integer(p, "v", 1, 1) != 1:
        raise ValueError("Unsupported telemetry version")
    integer(p, "id", 1, 255)
    for key in ("n", "ms"):
        integer(p, key, 0, 0xffffffff)
    out = dict(p)
    kind = p["type"]
    if kind == "imu":
        scales = {"mpu6050": (1/16384, 1/16.4), "lsm6ds3": (0.000122, 0.0175)}
        if p["model"] not in scales:
            raise ValueError("Unknown IMU scale")
        a, g = scales[p["model"]]
        out["acc_g"] = [x*a for x in vector(p, "a", 3, -32768, 32767)]
        out["gyro_dps"] = [x*g for x in vector(p, "g", 3, -32768, 32767)]
    elif kind == "vitals":
        out["temp_c"] = None if p["tc"] is None else integer(p,"tc",-4000,12500)/100
        out["bpm"] = integer(p,"bpm",0,65535) or None
        integer(p,"pulse",0,4095)
    elif kind == "gps":
        fix = integer(p,"fix",0,1)
        lat = integer(p,"lat_e7",-900000000,900000000)
        lon = integer(p,"lon_e7",-1800000000,1800000000)
        alt = integer(p,"alt_cm",-2147483648,2147483647)
        speed = integer(p,"speed_cms",0,65535)
        integer(p,"sats",0,255)
        out.update(lat=lat/1e7 if fix else None,lon=lon/1e7 if fix else None,
                   alt_m=alt/100 if fix else None,speed_mps=speed/100 if fix else None)
    elif kind == "status":
        integer(p,"sensor_v",3,4)
        flags = integer(p,"flags",0,65535)
        for key in ("dropped","gaps"):
            integer(p,key,0,0xffffffff)
        for bit,key in enumerate(("imu_online","analog_ready","temp_valid","bpm_valid","gps_fix","gps_enabled","imu_initializing","raw_mode")):
            out[key] = bool(flags & (1 << bit))
    elif kind == "quat":
        q = [x/(1<<30) for x in vector(p,"q30",4,-2147483648,2147483647)]
        norm = sum(x*x for x in q)
        if not 0.9 <= norm <= 1.1:
            raise ValueError("Invalid quaternion")
        out["quaternion_wxyz"] = [x/math.sqrt(norm) for x in q]
    else:
        raise ValueError("Unknown message type")
    return out

class StreamDecoder:
    """Reassemble arbitrary notification boundaries; discard malformed lines."""
    def __init__(self):
        self.buffer = bytearray()
        self.discarding = False
        self.errors = 0
    def feed(self, data):
        packets = []
        for b in data:
            if b == 10:
                if not self.discarding and self.buffer:
                    try:
                        packets.append(decode(self.buffer))
                    except (ValueError, KeyError, TypeError, UnicodeError):
                        self.errors += 1
                self.buffer.clear()
                self.discarding = False
            elif not self.discarding:
                if len(self.buffer) >= 223:
                    self.buffer.clear(); self.discarding = True; self.errors += 1
                else:
                    self.buffer.append(b)
        return packets
