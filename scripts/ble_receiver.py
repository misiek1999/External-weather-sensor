"""BLE receiver / diagnostic tool for the weather station.

Parses the manufacturer-specific weather payload and, by default, also logs
every advertisement seen so that missing devices are easy to diagnose:

  * no output at all            -> the local BLE stack sees no advertisements
                                   (adapter/radio problem on the PC side)
  * device visible, no weather  -> frames are received but the payload is
                                   different than expected
  * weather lines printed       -> everything works

Usage:
    python scripts/ble_receiver.py [--address XX:XX:XX:XX:XX:XX] [--timeout N]
"""

import argparse
import asyncio
import struct

from bleak import BleakScanner

COMPANY_ID = 0xFFFF


def parse_weather_data(data: bytes):
    msg_type, temp_centi, hum_centi, batt_mv = struct.unpack("<BhHH", data)
    return {
        "type": msg_type,
        "temperature": temp_centi / 100.0,
        "humidity": hum_centi / 100.0,
        "battery_mv": batt_mv,
    }


def detection_callback(device, advertisement_data, filter_address=None):
    mfg = advertisement_data.manufacturer_data

    if filter_address and device.address != filter_address:
        return

    if COMPANY_ID in mfg:
        try:
            weather = parse_weather_data(mfg[COMPANY_ID])
        except struct.error:
            weather = None
        print(
            f"[weather] {device.address} rssi={advertisement_data.rssi} "
            f"data={mfg[COMPANY_ID].hex()} -> {weather}"
        )
    else:
        details = {
            "name": device.name,
            "rssi": advertisement_data.rssi,
            "mfg": {hex(k): v.hex() for k, v in mfg.items()},
            "uuids": advertisement_data.service_uuids,
        }
        print(f"[adv] {device.address} {details}")


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", help="only show this BLE address")
    parser.add_argument("--timeout", type=int, default=3600, help="scan time in seconds")
    parser.add_argument(
        "--passive",
        action="store_true",
        help="use passive scanning (Windows sometimes hides non-scannable "
        "beacons in active mode)",
    )
    args = parser.parse_args()

    def callback(device, advertisement_data):
        detection_callback(device, advertisement_data, args.address)

    mode = "passive" if args.passive else "active"
    print(
        f"Scanning ({mode}) for {args.timeout} s. "
        f"{'Filtering on ' + args.address if args.address else 'Showing all advertisements.'}"
    )
    scanner = BleakScanner(callback, scanning_mode=mode)
    await scanner.start()
    await asyncio.sleep(args.timeout)
    await scanner.stop()


asyncio.run(main())
