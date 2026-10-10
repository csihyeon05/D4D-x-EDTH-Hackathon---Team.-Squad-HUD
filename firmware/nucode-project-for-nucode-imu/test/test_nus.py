import asyncio
import io
import json
import pathlib
import sys
import types
import unittest
from unittest.mock import patch
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from nus_protocol import StreamDecoder, decode, SERVICE, TX
from ble_receiver import receive

def wire(**kwargs):
    p=dict(v=1,id=1,n=0,type="vitals",ms=1000,tc=2220,bpm=0,pulse=0)
    p.update(kwargs)
    return (json.dumps(p,separators=(",",":"))+"\n").encode()

class NusTests(unittest.TestCase):
    def test_all_splits_and_coalescing(self):
        data=wire()+wire(n=1,tc=3467)
        for size in range(1,len(data)+1):
            d=StreamDecoder(); packets=[]
            for i in range(0,len(data),size): packets+=d.feed(data[i:i+size])
            self.assertEqual([p["temp_c"] for p in packets],[22.2,34.67])
            self.assertEqual(d.errors,0)
    def test_bad_line_overflow_and_reconnect_boundary(self):
        d=StreamDecoder()
        self.assertEqual(d.feed(b'x'*300),[])
        self.assertEqual(len(d.buffer),0)
        self.assertEqual(len(d.feed(b'\n'+wire())),1)
        self.assertEqual(d.errors,1)
        self.assertEqual(d.feed(b'{"v":'),[])
        self.assertEqual(len(d.feed(b'\n'+wire())),1)
        self.assertEqual(d.errors,2)
    def test_models_and_unknown(self):
        p=dict(v=1,id=2,n=0,type="imu",ms=0,model="lsm6ds3",a=[1000,-1000,0],g=[100,-100,0])
        self.assertEqual(decode(json.dumps(p))["gyro_dps"],[1.7500000000000002,-1.7500000000000002,0])
        p.update(model="mpu6050",a=[16384,0,0],g=[164,0,0])
        self.assertEqual(decode(json.dumps(p))["acc_g"],[1,0,0])
        p["model"]="wrong"
        with self.assertRaises(ValueError): decode(json.dumps(p))
    def test_nulls_and_schema(self):
        self.assertIsNone(decode(wire(tc=None))["temp_c"])
        self.assertIsNone(decode(wire())["bpm"])
        for args in ({"v":2},{"id":0},{"ms":-1},{"tc":False},{"pulse":5000}):
            with self.assertRaises(ValueError): decode(wire(**args))
    def test_active_uuids(self):
        text=(pathlib.Path(__file__).resolve().parents[1]/"src/protocol/uuids.h").read_text().lower()
        self.assertIn(SERVICE,text); self.assertIn(TX,text)

class ReceiverTests(unittest.IsolatedAsyncioTestCase):
    async def test_nus_chunks_recorded_and_reconnected(self):
        seen=[]
        class BleakError(Exception): pass
        class Scanner:
            @staticmethod
            async def find_device_by_filter(*args,**kwargs): return types.SimpleNamespace(address="test")
        class Client:
            def __init__(self,device,disconnected_callback):
                self.done=disconnected_callback
                self.services=types.SimpleNamespace(get_service=lambda _:object())
            async def __aenter__(self): return self
            async def __aexit__(self,*args): pass
            async def start_notify(self,uuid,callback):
                seen.append(uuid)
                data=wire()+wire(n=1)
                for i in range(0,len(data),20): callback(None,data[i:i+20])
                self.done(self)
        module=types.ModuleType("bleak"); module.BleakClient=Client; module.BleakScanner=Scanner
        errors=types.ModuleType("bleak.exc"); errors.BleakError=BleakError
        async def cancel(_): raise asyncio.CancelledError
        output=io.StringIO()
        with patch.dict(sys.modules,{"bleak":module,"bleak.exc":errors}),patch("ble_receiver.asyncio.sleep",cancel):
            with self.assertRaises(asyncio.CancelledError):
                await receive(types.SimpleNamespace(address=None,name="NU87-1"),output)
        self.assertEqual(seen,[TX]); self.assertEqual(len(output.getvalue().splitlines()),2)
