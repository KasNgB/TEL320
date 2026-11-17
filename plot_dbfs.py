import asyncio, struct, threading
from collections import deque
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from bleak import BleakClient, BleakScanner
import math, time

# Information to connect to BLE
# UUID of the characteristic the nRF notifies on (must match dataChar in the Arduino code)
CHAR = "19B10001-E8F2-537E-4F6C-D104768A1214"
# Advertised name used to find the nRF during scanning (must match BLE.setLocalName)
NAME = "nRF52840-dBFS"
N_MICS = 2                          # number of floats per notification (one per microphone)
COLORS = ["tab:blue", "tab:red"]

# One rolling window per mic. maxlen=200 means old samples drop off automatically
# once 200 are stored, so the plot shows only the most recent 200 values.
ys = [deque(maxlen=200) for _ in range(N_MICS)]
# Flag used to tell the BLE thread to shut down when the plot window closes
stop = threading.Event()

t0 = time.time()                    # reference time so the debug prints show seconds since start
n_total = n_bad = 0                 # counters: all received frames / frames with suspicious values

# Callback that bleak calls each time a notification arrives from the nRF.
# It runs in the BLE thread, not the plotting thread.
def on_data(_, data: bytearray):
    global n_total, n_bad
    # Each notification should hold 4 bytes per mic (one float32 each).
    # Shorter packets are incomplete, so ignore them.
    if len(data) < 4 * N_MICS:
        return
    # "<2f" = little-endian, two 32-bit floats. This reinterprets the raw bytes as floats.
    # It matches the STM32 and nRF byte order, so no conversion is needed.
    vals = struct.unpack_from(f"<{N_MICS}f", data)
    n_total += 1
    for mic, v in enumerate(vals):
        ys[mic].append(v)

# Runs the BLE connection using asyncio (bleak is async-only).
async def ble_main():
    # Scan until a device advertising this name is found (returns None on timeout)
    dev = await BleakScanner.find_device_by_name(NAME)
    if dev is None:
        print("Device not found")
        return
    # "async with" connects now and automatically disconnects when the block exits
    async with BleakClient(dev) as client:
        # Subscribe to notifications: bleak now calls on_data for every notification
        await client.start_notify(CHAR, on_data)
        # Keep the connection alive until the main thread sets "stop".
        # The short sleep lets asyncio process incoming notifications meanwhile.
        while not stop.is_set():
            await asyncio.sleep(0.2)

# Run the BLE code in a background thread with its own asyncio event loop, so it doesn't
# block matplotlib's GUI loop. daemon=True means the thread dies when the program exits.
threading.Thread(target=lambda: asyncio.run(ble_main()), daemon=True).start()

# Set up the plot: one empty line per mic, filled with data later by update()
fig, ax = plt.subplots()
lines = [ax.plot([], [], color=COLORS[m], label=f"mic {m}")[0] for m in range(N_MICS)]
ax.set_ylabel("dBFS")
ax.set_ylim(-100, 0)                # initial y-range; note that update() overrides it with autoscale
ax.legend(loc="upper right")

# Called by FuncAnimation every 50 ms to refresh the lines with the latest data
def update(_):
    for m, line in enumerate(lines):
        data = list(ys[m])          # snapshot to avoid mutation mid-draw (the BLE thread keeps appending)
        # x-axis is just the sample index (0..N-1), not real time
        line.set_data(range(len(data)), data)
    # Recompute the axis limits from the new data. This is why the y-axis rescales, and why a
    # single bad value (e.g. -3e38) squashes the plot into a flat line with huge spikes.
    ax.relim()
    ax.autoscale_view()
    return tuple(lines)

# interval=50 -> redraw 20 times per second.
# cache_frame_data=False because the animation has no fixed number of frames.
ani = FuncAnimation(fig, update, interval=50, cache_frame_data=False)
plt.show()                          # blocks until you close the window
stop.set()                          # tells the BLE thread to disconnect