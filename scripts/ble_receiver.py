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
import platform
import struct
import time

from bleak import BleakScanner

COMPANY_ID = 0xFFFF
MSG_TYPE_WEATHER = 0x01
MSG_TYPE_ERROR = 0xEE
FLAG_SENSOR_OK = 1 << 0
ERROR_CRITICAL_LOW_BATTERY = 0x01
ERROR_INIT_SENSOR_FAILURE = 0x02
ERROR_READ_SENSOR_FAILURE = 0x04

LOCATION_NAMES = {
    0: "UNKNOWN",
    1: "OUTDOOR_1",
    2: "OUTDOOR_2",
    3: "OUTDOOR_3",
    4: "LIVING_ROOM",
    5: "BEDROOM",
    6: "KITCHEN",
    7: "TOILET",
}


def parse_weather_data(data: bytes):
    # Current payload (v4): <BBhHHBH> -> msg_type, flags, temp, hum, batt, location, sequence
    if len(data) == struct.calcsize("<BBhHHBH"):
        msg_type, flags, temp_centi, hum_centi, batt_mv, location, sequence = struct.unpack(
            "<BBhHHBH", data
        )

        result = {
            "type": msg_type,
            "flags": flags,
            "battery_mv": batt_mv,
            "location": location,
            "location_name": LOCATION_NAMES.get(location, f"UNKNOWN_{location}"),
            "sequence": sequence,
        }

        if msg_type == MSG_TYPE_ERROR:
            result["format"] = "v4-error"
            result["error_code"] = flags
            result["error_name"] = {
                ERROR_CRITICAL_LOW_BATTERY: "CRITICAL_LOW_BATTERY",
                ERROR_INIT_SENSOR_FAILURE: "INIT_SENSOR_FAILURE",
                ERROR_READ_SENSOR_FAILURE: "READ_SENSOR_FAILURE",
            }.get(flags, f"UNKNOWN_ERROR_{flags}")
            return result

        result.update(
            {
                "sensor_ok": bool(flags & FLAG_SENSOR_OK),
                "temperature": temp_centi / 100.0,
                "humidity": hum_centi / 100.0,
                "format": "v4",
            }
        )
        return result

    # Older payload (v3): <BBhHHH> -> msg_type, flags, temp, hum, batt, sequence
    if len(data) == struct.calcsize("<BBhHHH"):
        msg_type, flags, temp_centi, hum_centi, batt_mv, sequence = struct.unpack("<BBhHHH", data)

        if msg_type == MSG_TYPE_ERROR:
            error_name = {
                ERROR_CRITICAL_LOW_BATTERY: "CRITICAL_LOW_BATTERY",
                ERROR_INIT_SENSOR_FAILURE: "INIT_SENSOR_FAILURE",
                ERROR_READ_SENSOR_FAILURE: "READ_SENSOR_FAILURE",
            }.get(flags, f"UNKNOWN_ERROR_{flags}")
            return {
                "type": msg_type,
                "error_code": flags,
                "error_name": error_name,
                "battery_mv": batt_mv,
                "sequence": sequence,
                "format": "v3-error",
            }

        return {
            "type": msg_type,
            "flags": flags,
            "sensor_ok": bool(flags & FLAG_SENSOR_OK),
            "temperature": temp_centi / 100.0,
            "humidity": hum_centi / 100.0,
            "battery_mv": batt_mv,
            "sequence": sequence,
            "format": "v3",
        }

    # New payload (v2): <BBhHH> -> msg_type, flags, temp, hum, batt
    if len(data) == struct.calcsize("<BBhHH"):
        msg_type, flags, temp_centi, hum_centi, batt_mv = struct.unpack("<BBhHH", data)
        return {
            "type": msg_type,
            "flags": flags,
            "sensor_ok": bool(flags & FLAG_SENSOR_OK),
            "temperature": temp_centi / 100.0,
            "humidity": hum_centi / 100.0,
            "battery_mv": batt_mv,
            "sequence": None,
            "format": "v2",
        }

    # Legacy payload (v1): <BhHH> -> msg_type, temp, hum, batt
    if len(data) == struct.calcsize("<BhHH"):
        msg_type, temp_centi, hum_centi, batt_mv = struct.unpack("<BhHH", data)
        return {
            "type": msg_type,
            "flags": None,
            "sensor_ok": None,
            "temperature": temp_centi / 100.0,
            "humidity": hum_centi / 100.0,
            "battery_mv": batt_mv,
            "sequence": None,
            "format": "v1",
        }

    raise struct.error(f"Unsupported payload size: {len(data)}")


def detection_callback(
    device,
    advertisement_data,
    filter_address=None,
    show_all_advertisements=False,
    seen_state=None,
    dedup_state=None,
    dedup_ms=1200,
    expected_period_s=300,
    gap_warn_factor=1.6,
    emit_duplicates=False,
):
    mfg = advertisement_data.manufacturer_data

    if filter_address and device.address != filter_address:
        return

    now_monotonic = time.monotonic()
    now_wall = time.strftime("%H:%M:%S")

    if COMPANY_ID in mfg:
        payload_raw = mfg[COMPANY_ID]
        try:
            weather = parse_weather_data(payload_raw)
        except struct.error:
            weather = None

        if dedup_state is not None:
            sequence = weather.get("sequence") if weather else None
            dedup_key = (device.address, sequence if sequence is not None else payload_raw)
            last_print = dedup_state.get(dedup_key)
            if (
                not emit_duplicates
                and last_print is not None
                and (now_monotonic - last_print) * 1000.0 < dedup_ms
            ):
                return
            dedup_state[dedup_key] = now_monotonic

        gap_s = None
        if seen_state is not None:
            prev = seen_state.get(device.address)
            if prev is not None:
                gap_s = now_monotonic - prev
            seen_state[device.address] = now_monotonic

        gap_text = f" gap={gap_s:.2f}s" if gap_s is not None else " gap=first"
        print(
            f"[{now_wall}] [weather] {device.address} rssi={advertisement_data.rssi}{gap_text} "
            f"data={payload_raw.hex()} -> {weather}"
        )
        warn_gap_s = expected_period_s * gap_warn_factor
        if gap_s is not None and gap_s > warn_gap_s:
            print(
                f"[{now_wall}] [warn] Long gap detected ({gap_s:.1f}s > {warn_gap_s:.1f}s). "
                "This usually means scanner missed one or more advertising bursts."
            )

    elif show_all_advertisements:
        details = {
            "name": device.name,
            "rssi": advertisement_data.rssi,
            "mfg": {hex(k): v.hex() for k, v in mfg.items()},
            "uuids": advertisement_data.service_uuids,
        }
        print(f"[{now_wall}] [adv] {device.address} {details}")


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", help="only show this BLE address")
    parser.add_argument("--timeout", type=int, default=3600, help="scan time in seconds")
    parser.add_argument(
        "--expected-period-s",
        type=int,
        default=300,
        help="expected interval between fresh sensor updates in seconds (default: 300)",
    )
    parser.add_argument(
        "--gap-warn-factor",
        type=float,
        default=1.6,
        help="warn when observed gap is greater than expected-period-s * this factor",
    )
    parser.add_argument(
        "--dedup-ms",
        type=int,
        default=360000,
        help="suppress duplicate payload prints from the same address within this time window (ms)",
    )
    parser.add_argument(
        "--show-duplicates",
        action="store_true",
        help="print repeated advertisements for the same payload/sequence instead of suppressing them",
    )
    parser.add_argument(
        "--all-adv",
        action="store_true",
        help="print all advertisements, not only weather frames",
    )
    parser.add_argument(
        "--active",
        action="store_true",
        help="force active scan mode",
    )
    parser.add_argument(
        "--passive",
        action="store_true",
        help="use passive scanning (Windows sometimes hides non-scannable "
        "beacons in active mode)",
    )
    args = parser.parse_args()

    if args.expected_period_s <= 0:
        parser.error("--expected-period-s must be > 0")
    if args.gap_warn_factor <= 1.0:
        parser.error("--gap-warn-factor must be > 1.0")
    if args.dedup_ms < 0:
        parser.error("--dedup-ms must be >= 0")

    if args.active and args.passive:
        parser.error("Choose only one of --active or --passive")

    default_passive = platform.system().lower() == "windows"
    if args.active:
        mode = "active"
    elif args.passive:
        mode = "passive"
    else:
        mode = "passive" if default_passive else "active"

    seen_state = {}
    dedup_state = {}
    unique_weather_addrs = set()
    warned_many_devices = False

    def callback(device, advertisement_data):
        nonlocal warned_many_devices

        if COMPANY_ID in advertisement_data.manufacturer_data:
            unique_weather_addrs.add(device.address)
            if len(unique_weather_addrs) > 1 and not warned_many_devices and not args.address:
                warned_many_devices = True
                print(
                    "[hint] Multiple devices with matching weather payload detected. "
                    "Use --address XX:XX:XX:XX:XX:XX to lock to one sensor."
                )

        detection_callback(
            device,
            advertisement_data,
            filter_address=args.address,
            show_all_advertisements=args.all_adv,
            seen_state=seen_state,
            dedup_state=dedup_state,
            dedup_ms=args.dedup_ms,
            expected_period_s=args.expected_period_s,
            gap_warn_factor=args.gap_warn_factor,
            emit_duplicates=args.show_duplicates,
        )

    print(
        f"Scanning ({mode}) for {args.timeout} s. "
        f"{'Filtering on ' + args.address if args.address else 'Showing weather frames.'}"
    )
    scanner = BleakScanner(callback, scanning_mode=mode)
    await scanner.start()
    await asyncio.sleep(args.timeout)
    await scanner.stop()


asyncio.run(main())
