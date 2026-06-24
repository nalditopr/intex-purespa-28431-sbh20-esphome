"""
Stream an ESPHome device's logs over its native API from a PC, no ESPHome CLI needed.
Retries until the device comes online, so you can start it before powering the ESP.

Edit ADDR and NOISE_PSK below (NOISE_PSK = the api: encryption key from secrets.yaml).
Run:  python esp_log_monitor.py
Requires:  pip install aioesphomeapi
"""
import asyncio
from datetime import datetime
from aioesphomeapi import APIClient, LogLevel

ADDR = "intex-spa.local"          # or the device IP, e.g. "192.168.1.50"
PORT = 6053
NOISE_PSK = "PASTE_YOUR_api_key_HERE="   # the api: encryption key
OUT = "esp_log.txt"

def stamp():
    return datetime.now().strftime("%H:%M:%S")

def write(line):
    print(line, flush=True)
    try:
        with open(OUT, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass

async def main():
    write(f"[{stamp()}] waiting for {ADDR} ...")
    while True:
        cli = APIClient(ADDR, PORT, "", noise_psk=NOISE_PSK)
        try:
            await cli.connect(login=True)
        except Exception:
            await asyncio.sleep(2)
            continue
        write(f"[{stamp()}] CONNECTED - streaming logs")

        def on_log(msg):
            text = msg.message.decode("utf-8", "replace") if isinstance(msg.message, bytes) else str(msg.message)
            write(f"[{stamp()}] {text}")

        try:
            cli.subscribe_logs(on_log, log_level=LogLevel.LOG_LEVEL_DEBUG)
            while True:
                await asyncio.sleep(1)
        except Exception as e:
            write(f"[{stamp()}] disconnected: {e!r} - reconnecting")
            try:
                await cli.disconnect()
            except Exception:
                pass
            await asyncio.sleep(2)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
