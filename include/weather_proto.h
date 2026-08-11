#ifndef WEATHER_PROTO_H_
#define WEATHER_PROTO_H_

#include <stdint.h>
#include <string.h>

/* 0xFFFF is a Bluetooth SIG reserved identifier for test/internal
 * purposes. It is fine for a private, non-commercial project. */
#define WEATHER_BLE_COMPANY_ID   0xFFFF
#define WEATHER_BLE_MSG_TYPE_WEATHER 0x01
#define WEATHER_BLE_MSG_TYPE_ERROR   0xEE

#define WEATHER_BLE_FLAG_SENSOR_OK (1u << 0)
#define WEATHER_BLE_ERROR_CRITICAL_LOW_BATTERY  0x01
#define WEATHER_BLE_INIT_SENSOR_FAILURE        0x02
#define WEATHER_BLE_READ_SENSOR_FAILURE        0x04

/*
 * Frame format (Manufacturer Specific Data), all little-endian:
 *
 * Bytes 0-1 : Company ID     (uint16)  = 0xFFFF
 * Byte  2   : msg_type       (uint8)   = 0x01
 * Byte  3   : flags          (uint8)   -> bit0: sensor data valid
 * Bytes 4-5 : temperature    (int16)   -> value * 100 (e.g. 2345 = 23.45 C)
 * Bytes 6-7 : humidity       (uint16)  -> value * 100 (e.g. 4567 = 45.67 %)
 * Bytes 8-9 : battery volt.  (uint16)  -> mV
 * Bytes 10-11 : sequence     (uint16)  -> increments once per fresh sample cycle
 *
 * 12 bytes in total.
 */
struct weather_ble_payload {
    uint8_t  msg_type;
    uint8_t  flags;
    int16_t  temperature_c;
    uint16_t humidity_pct;
    uint16_t battery_mv;
    uint16_t sequence;
} __packed;

#define WEATHER_BLE_PACKET_LEN (2 + sizeof(struct weather_ble_payload))

static inline void weather_ble_encode(uint8_t *buf,
                                       uint8_t flags,
                                       int16_t temp_centi,
                                       uint16_t hum_centi,
                                       uint16_t battery_mv,
                                       uint16_t sequence)
{
    struct weather_ble_payload payload = {
        .msg_type       = WEATHER_BLE_MSG_TYPE_WEATHER,
        .flags          = flags,
        .temperature_c  = temp_centi,
        .humidity_pct   = hum_centi,
        .battery_mv     = battery_mv,
        .sequence       = sequence,
    };

    buf[0] = (uint8_t)(WEATHER_BLE_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)((WEATHER_BLE_COMPANY_ID >> 8) & 0xFF);
    memcpy(&buf[2], &payload, sizeof(payload));
}

static inline void weather_ble_encode_error(uint8_t *buf,
                                             uint8_t error_code,
                                             uint16_t battery_mv,
                                             uint16_t sequence)
{
    struct weather_ble_payload payload = {
        .msg_type       = WEATHER_BLE_MSG_TYPE_ERROR,
        .flags          = error_code,
        .temperature_c  = 0,
        .humidity_pct   = 0,
        .battery_mv     = battery_mv,
        .sequence       = sequence,
    };

    buf[0] = (uint8_t)(WEATHER_BLE_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)((WEATHER_BLE_COMPANY_ID >> 8) & 0xFF);
    memcpy(&buf[2], &payload, sizeof(payload));
}

#endif /* WEATHER_PROTO_H_ */