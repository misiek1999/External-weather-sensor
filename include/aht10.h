#ifndef AHT10_H_
#define AHT10_H_

#include <drivers/i2c.h>
#include <stdint.h>

/* Initialize + calibrate the sensor. Call after every VCC power-up. */
int aht10_init(const struct i2c_dt_spec *i2c);

/* Trigger a measurement and read the results.
 * temp_centi/hum_centi are in hundredths of a unit (e.g. 2345 = 23.45C) */
int aht10_read(const struct i2c_dt_spec *i2c, int32_t *temp_centi, int32_t *hum_centi);

#endif /* AHT10_H_ */