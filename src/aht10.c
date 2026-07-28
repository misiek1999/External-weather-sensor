#include "aht10.h"
#include <kernel.h>
#include <errno.h>

#define AHT10_CMD_CALIBRATE     0xE1
#define AHT10_CMD_TRIGGER       0xAC
#define AHT10_CMD_SOFTRESET     0xBA
#define AHT10_STATUS_BUSY       BIT(7)

int aht10_init(const struct i2c_dt_spec *i2c)
{
    int err;
    uint8_t reset_cmd = AHT10_CMD_SOFTRESET;
    uint8_t calib_cmd[3] = { AHT10_CMD_CALIBRATE, 0x08, 0x00 };

    err = i2c_write_dt(i2c, &reset_cmd, 1);
    if (err) {
        return err;
    }
    k_sleep(K_MSEC(20));

    err = i2c_write_dt(i2c, calib_cmd, sizeof(calib_cmd));
    if (err) {
        return err;
    }
    k_sleep(K_MSEC(10));

    return 0;
}

int aht10_read(const struct i2c_dt_spec *i2c, int32_t *temp_centi, int32_t *hum_centi)
{
    int err;
    uint8_t trigger_cmd[3] = { AHT10_CMD_TRIGGER, 0x33, 0x00 };
    uint8_t data[6];

    err = i2c_write_dt(i2c, trigger_cmd, sizeof(trigger_cmd));
    if (err) {
        return err;
    }

    /* Measurement time per the datasheet is ~80 ms */
    k_sleep(K_MSEC(80));

    err = i2c_read_dt(i2c, data, sizeof(data));
    if (err) {
        return err;
    }

    if (data[0] & AHT10_STATUS_BUSY) {
        return -EBUSY;
    }

    uint32_t raw_hum  = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    /* RH[%]  = raw / 2^20 * 100 */
    *hum_centi  = (int32_t)(((uint64_t)raw_hum * 10000) / 1048576ULL);

    /* T[C]   = raw / 2^20 * 200 - 50 */
    *temp_centi = (int32_t)((((int64_t)raw_temp * 20000) / 1048576LL) - 5000);

    return 0;
}