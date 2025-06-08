import asyncio
import sys
from bleak import BleakClient, BleakScanner

NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Write
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # Notify

async def main(target_name):
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    target = None
    for d in devices:
        print(f"{d.address} - {d.name}")
        if d.name == target_name or d.address == target_name:
            target = d
    if not target:
        print("Target device not found.")
        return

    client = BleakClient(target.address)
    message_queue = asyncio.Queue()
    stop_event = asyncio.Event()

    def handle_notify(sender, data):
        asyncio.get_event_loop().call_soon_threadsafe(message_queue.put_nowait, data)

    try:
        await client.__aenter__()
        print(f"Connected to {target.address}")

        try:
            await client.start_notify(NUS_TX_CHAR_UUID, handle_notify)
        except Exception as e:
            print(f"Notification setup failed: {e}")

        print("Type messages to send. Type 'exit' to quit.")

        async def input_loop():
            while True:
                msg = await asyncio.get_event_loop().run_in_executor(None, input, "---------------------------------\n")
                if msg.lower() == "exit":
                    stop_event.set()
                    break
                await client.write_gatt_char(NUS_RX_CHAR_UUID, msg.encode())
                await asyncio.sleep(0.05)

        async def output_loop():
            while not stop_event.is_set():
                try:
                    data = await asyncio.wait_for(message_queue.get(), timeout=0.1)
                    print(f"{data.decode(errors='replace')}", end="", flush=True)
                except asyncio.TimeoutError:
                    continue

        await asyncio.gather(input_loop(), output_loop())

        try:
            await client.stop_notify(NUS_TX_CHAR_UUID)
        except Exception:
            pass

    finally:
        print("Disconnecting device...")
        await client.disconnect()

async def scan_devices():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    if not devices:
        print("No BLE devices found.")
        return
    print("Found BLE devices:")
    for d in devices:
        print(f"{d.address} - {d.name}")

def run_main():
    if len(sys.argv) < 2:
        asyncio.run(scan_devices())
        print("\nUsage: python ble_terminal.py <DEVICE_NAME_OR_ADDRESS>")
        sys.exit(0)
    asyncio.run(main(sys.argv[1]))

if __name__ == "__main__":
    run_main()