import asyncio, struct, threading
from collections import deque
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from bleak import BleakClient, BleakScanner
import math, struct, time

CHAR = "19B10001-E8F2-537E-4F6C-D104768A1214"
NAME = "nRF52840-dBFS"

ys = deque(maxlen=200)          # rolling window of latest values
stop = threading.Event()

t0 = time.time()
n_total = n_bad = 0

def on_data(_, data: bytearray):
    global n_total, n_bad
    (v,) = struct.unpack_from("<f", data)
    n_total += 1
    if math.isnan(v) or v > -10 or v < -200:
        n_bad += 1
        print(f"{time.time()-t0:7.2f}s BAD v={v!r} bytes={data.hex()}")
    ys.append(v)     # still plot it

async def ble_main():
    dev = await BleakScanner.find_device_by_name(NAME)
    if dev is None:
        print("Device not found")
        return
    async with BleakClient(dev) as client:
        await client.start_notify(CHAR, on_data)
        while not stop.is_set():
            await asyncio.sleep(0.2)

threading.Thread(target=lambda: asyncio.run(ble_main()), daemon=True).start()

fig, ax = plt.subplots()
(line,) = ax.plot([], [])
ax.set_ylabel("dBFS")
ax.set_ylim(-100, 0)  

def update(_):
    data = list(ys)             # snapshot to avoid mutation mid-draw
    line.set_data(range(len(data)), data)
    ax.relim()
    ax.autoscale_view()
    return (line,)

ani = FuncAnimation(fig, update, interval=50, cache_frame_data=False)
plt.show()                      # blocks until you close the window
stop.set()                      # tells the BLE thread to disconnect