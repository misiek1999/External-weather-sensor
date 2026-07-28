#ifndef WEATHER_PROTO_H_
#define WEATHER_PROTO_H_

#include <stdint.h>
#include <string.h>

/* 0xFFFF is a Bluetooth SIG reserved identifier for test/internal
 * purposes. It is fine for a private, non-commercial project. */
#define WEATHER_BLE_COMPANY_ID   0xFFFF
#define WEATHER_BLE_MSG_TYPE     0x01

/*
 * Frame format (Manufacturer Specific Data), all little-endian:
 *
 * Bytes 0-1 : Company ID     (uint16)  = 0xFFFF
 * Byte  2   : msg_type       (uint8)   = 0x01
 * Bytes 3-4 : temperature    (int16)   -> value * 100 (e.g. 2345 = 23.45 C)
 * Bytes 5-6 : humidity       (uint16)  -> value * 100 (e.g. 4567 = 45.67 %)
 * Bytes 7-8 : battery volt.  (uint16)  -> mV
 *
 * 9 bytes in total.
 */
struct weather_ble_payload {
    uint8_t  msg_type;
    int16_t  temperature_c;
    uint16_t humidity_pct;
    uint16_t battery_mv;
} __packed;

#define WEATHER_BLE_PACKET_LEN (2 + sizeof(struct weather_ble_payload))

static inline void weather_ble_encode(uint8_t *buf,
                                       int16_t temp_centi,
                                       uint16_t hum_centi,
                                       uint16_t battery_mv)
{
    struct weather_ble_payload payload = {
        .msg_type       = WEATHER_BLE_MSG_TYPE,
        .temperature_c  = temp_centi,
        .humidity_pct   = hum_centi,
        .battery_mv     = battery_mv,
    };

    buf[0] = (uint8_t)(WEATHER_BLE_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)((WEATHER_BLE_COMPANY_ID >> 8) & 0xFF);
    memcpy(&buf[2], &payload, sizeof(payload));
}

#endif /* WEATHER_PROTO_H_ */