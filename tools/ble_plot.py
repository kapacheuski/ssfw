import asyncio
import sys
import json
from bleak import BleakClient, BleakScanner
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import threading

NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Write
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # Notify

HIST_LEN = 200
temp_data = deque(maxlen=HIST_LEN)
imu_temp_data = deque(maxlen=HIST_LEN)
acc_data = [deque(maxlen=HIST_LEN) for _ in range(3)]
gyro_data = [deque(maxlen=HIST_LEN) for _ in range(3)]
data_lock = threading.Lock()

def init_plots():
    fig, axs = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
    l_temp, = axs[0].plot([], [], label="temperature")
    l_imu_temp, = axs[0].plot([], [], label="imu_temp")
    axs[0].legend()
    axs[0].set_ylabel("Temp (C)")
    axs[0].set_title("Temperature and IMU Temp")
    l_acc = [axs[1].plot([], [], label=lbl)[0] for lbl in ['acc_x', 'acc_y', 'acc_z']]
    axs[1].legend()
    axs[1].set_ylabel("Acceleration (g)")
    axs[1].set_title("Accelerometer")
    l_gyro = [axs[2].plot([], [], label=lbl)[0] for lbl in ['gyro_x', 'gyro_y', 'gyro_z']]
    axs[2].legend()
    axs[2].set_ylabel("Gyro (dps)")
    axs[2].set_title("Gyroscope")
    axs[2].set_xlabel("Samples")
    plt.tight_layout()
    return fig, axs, l_temp, l_imu_temp, l_acc, l_gyro

def update_plots(frame, l_temp, l_imu_temp, l_acc, l_gyro):
    with data_lock:
        x = range(len(temp_data))
        l_temp.set_data(x, list(temp_data))
        l_imu_temp.set_data(x, list(imu_temp_data))
        for i in range(3):
            l_acc[i].set_data(x, list(acc_data[i]))
            l_gyro[i].set_data(x, list(gyro_data[i]))
        for ax in l_temp.axes.figure.axes:
            ax.relim()
            ax.autoscale_view()
    return [l_temp, l_imu_temp] + l_acc + l_gyro

async def ble_task(target_name):
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    target = None
    for d in devices:
        if d.name == target_name or d.address == target_name:
            target = d
    if not target:
        print("Target device not found.")
        return

    async with BleakClient(target.address) as client:
        print(f"Connected to {target.address}")

        def handle_notify(sender, data):
            try:
                msg = data.decode(errors='replace')
                js = json.loads(msg)
               # print(f"Received data: {js}")  # Console output now works in real time
                with data_lock:
                    if "temperature" in js:
                        temp_data.append(js.get("temperature", 0))
                    if "imu_temp" in js:
                        imu_temp_data.append(js.get("imu_temp", 0))
                    if "acc" in js:
                        for i in range(3):
                            acc_data[i].append(js["acc"][i])
                    if "gyro" in js:
                        for i in range(3):
                            gyro_data[i].append(js["gyro"][i])
            except Exception as e:
                print(f"JSON parse error: {e}")

        await client.start_notify(NUS_TX_CHAR_UUID, handle_notify)
        while True:
            await asyncio.sleep(0.1)  # Keep BLE running

def run_ble_loop(target_name):
    asyncio.run(ble_task(target_name))

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python ble_plot.py <DEVICE_NAME_OR_ADDRESS>")
        sys.exit(1)

    # Start BLE in a background thread
    ble_thread = threading.Thread(target=run_ble_loop, args=(sys.argv[1],), daemon=True)
    ble_thread.start()

    # Start plotting in main thread
    fig, axs, l_temp, l_imu_temp, l_acc, l_gyro = init_plots()
    ani = FuncAnimation(fig, update_plots, fargs=(l_temp, l_imu_temp, l_acc, l_gyro), interval=100)
    plt.show()