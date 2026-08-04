#include <kernel.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <drivers/adc.h>
#include <pm/device.h>
#include <errno.h>
#include <bluetooth/bluetooth.h>
#include <sys/printk.h>
#include <hal/nrf_saadc.h>

#include "aht10.h"
#include "weather_proto.h"
#define I2C0_NODE DT_NODELABEL(i2c0)

/* ---------- Timing configuration ---------- */
#define SLEEP_TIME_MS     (5 * 60 * 1000)  /* 5 minutes */
#define ADV_DURATION_MS   500  /* temporary: longer window so the scanner catches it; revert to ~1000 */
#define SENSOR_WARMUP_MS  200

/* ---------- Sensor power pin (VCC) ---------- */
static const struct gpio_dt_spec sensor_pwr =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), sensor_pwr_gpios);

/* ---------- AHT10 na I2C ---------- */
static const struct i2c_dt_spec aht10 = I2C_DT_SPEC_GET(DT_NODELABEL(aht10));

/* ---------- ADC - battery voltage measurement ---------- */
static const struct device *adc_dev = DEVICE_DT_GET_ONE(nordic_nrf_saadc);

#define BATT_CHANNEL_ID    2
#define BATT_ADC_INPUT     NRF_SAADC_INPUT_VDDHDIV5
#define BATT_ADC_RESOLUTION 12

static const struct adc_channel_cfg batt_channel_cfg = {
    .gain             = ADC_GAIN_1_3,
    .reference        = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
    .channel_id       = BATT_CHANNEL_ID,
    .input_positive   = BATT_ADC_INPUT,
};

static int16_t sample_buffer;

static const struct adc_sequence sequence = {
    .channels     = BIT(BATT_CHANNEL_ID),
    .buffer       = &sample_buffer,
    .buffer_size  = sizeof(sample_buffer),
    .resolution   = BATT_ADC_RESOLUTION,
};

/* ---------- BLE packet buffer ---------- */
static uint8_t mfg_data[WEATHER_BLE_PACKET_LEN];

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};

/* ---------- Console UART power management ---------- */
/*
 * The console UART keeps the UARTE peripheral active (and with it the
 * whole wake domain powered), which dominates the current drawn while the
 * device sleeps. Suspend it explicitly for the duration of the long sleep
 * and resume it before any further printk() calls.
 */
static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

static void uart_suspend(void)
{
    int err = pm_device_state_set(uart_dev, PM_DEVICE_STATE_SUSPENDED);

    if (err < 0 && err != -EALREADY) {
        printk("Failed to suspend console UART (err %d)\n", err);
    }
}

static void uart_resume(void)
{
    int err = pm_device_state_set(uart_dev, PM_DEVICE_STATE_ACTIVE);

    if (err < 0 && err != -EALREADY) {
        printk("Failed to resume console UART (err %d)\n", err);
    }
}

static uint16_t read_battery_mv(void)
{
    int32_t val_mv;
    int err = adc_read(adc_dev, &sequence);

    if (err) {
        printk("ADC read error: %d\n", err);
        return 0;
    }

    val_mv = sample_buffer;
    adc_raw_to_millivolts(adc_ref_internal(adc_dev), batt_channel_cfg.gain, BATT_ADC_RESOLUTION, &val_mv);

    // VDDH is divided by 5, so we multiply the sample by 5 to get the actual voltage in mV
    val_mv *= 5;

    return (uint16_t)val_mv;
}

static void print_fixed(const char *label, int32_t centi, const char *unit)
{
    int32_t whole = centi / 100;
    int32_t frac  = centi % 100;

    if (frac < 0) {
        frac = -frac;
    }

    printk("%s: %d.%02d %s\n", label, whole, frac, unit);
}

/*
 * Sleep until the next multiple of slot_ms of k_uptime_get(). Anchoring the
 * sleep to absolute time keeps the broadcast schedule on a fixed 5-minute
 * grid; a plain k_sleep() would make every cycle last 5 minutes plus the
 * wake-up work time and the schedule would drift further away each cycle.
 */
static void sleep_to_slot(uint32_t slot_ms)
{
    const int64_t now_ms = k_uptime_get();
    const int64_t next_ms = ((now_ms / slot_ms) + 1) * slot_ms;
    const int32_t remaining_ms = (int32_t)(next_ms - now_ms);

    if (remaining_ms > 0) {
        k_sleep(K_MSEC(remaining_ms));
    } else {
        printk("Warning: sleep_to_slot() called too late, remaining_ms=%d\n", remaining_ms);
    }
}

static void broadcast_weather_data(int32_t temp_centi, int32_t hum_centi, uint16_t battery_mv)
{
    int err;

    weather_ble_encode(mfg_data, (int16_t)temp_centi, (uint16_t)hum_centi, battery_mv);

    err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Unable to start BLE advertising (err %d)\n", err);
        return;
    }

    k_sleep(K_MSEC(ADV_DURATION_MS));

    err = bt_le_adv_stop();
    if (err) {
        printk("Unable to stop BLE advertising (err %d)\n", err);
    }
}


int main(void)
{
    int err;

    printk("\n=== External weather station start ===\n");

    if (!device_is_ready(sensor_pwr.port)) {
        printk("Power control pin not ready!\n");
        return;
    }
    gpio_pin_configure_dt(&sensor_pwr, GPIO_OUTPUT_INACTIVE);

    if (!device_is_ready(aht10.bus)) {
        printk("I2C bus for AHT10 not ready!\n");
        return;
    }

    if (!device_is_ready(adc_dev)) {
        printk("ADC not ready!\n");
        return;
    }

    err = adc_channel_setup(adc_dev, &batt_channel_cfg);
    if (err) {
        printk("ADC channel setup error: %d\n", err);
        return;
    }

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth initialization error (err %d)\n", err);
        return;
    }
    printk("Bluetooth initialized\n");

    bt_addr_le_t addr;
    size_t addr_count = 1;
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_id_get(&addr, &addr_count);
    if (addr_count > 0) {
        bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));
        printk("BLE MAC: %s\n", addr_str);
    }

    while (1) {
        int32_t  temp_centi = 0;
        int32_t  hum_centi  = 0;
        uint16_t battery_mv;

        /* 1. Power up the sensor */
        gpio_pin_set_dt(&sensor_pwr, 1);
        k_sleep(K_MSEC(SENSOR_WARMUP_MS));

        /* 2. Initialize/calibrate and read the AHT10 - we have to do this
         *    every time because we completely cut its power supply */
        int ret = aht10_init(&aht10);
        if (ret != 0) {
            printk("AHT10 init error: %d\n", ret);
        } else {
            ret = aht10_read(&aht10, &temp_centi, &hum_centi);
            if (ret != 0) {
                printk("AHT10 read error: %d\n", ret);
            } else {
                print_fixed("Temp", temp_centi, "C");
                print_fixed("Humidity", hum_centi, "%");
            }
        }

        /* 3. Power down the sensor - energy saving */
        gpio_pin_set_dt(&sensor_pwr, 0);

        /* 4. Battery voltage */
        battery_mv = read_battery_mv();
        printk("Battery voltage: %u mV\n", battery_mv);

        /* 5. Broadcast data over BLE (connectionless advertising) */
        broadcast_weather_data(temp_centi, hum_centi, battery_mv);

        /* 6. Sleep until the next 5-minute slot with the console suspended,
         *    so the SoC can reach its lowest idle current */
        printk("Sleep for %d minutes\n", SLEEP_TIME_MS / 60000);
        uart_suspend();
        sleep_to_slot(SLEEP_TIME_MS);
        uart_resume();
    }

    return 0;
}