#include <stdint.h>

#include "log.h"

#if defined(CONFIG_PRINTK) && CONFIG_PRINTK

/* Runtime per-module levels, initialized from the log_config.h defaults. */
uint8_t log_runtime_levels[LOG_MOD_COUNT] = {
#define _X(_tag, _level) [LOG_MOD_##_tag] = (_level),
	LOG_MODULES(_X)
#undef _X
};

void log_set_level(uint8_t module_id, uint8_t level)
{
	if (module_id < LOG_MOD_COUNT) {
		log_runtime_levels[module_id] = level;
	}
}

uint8_t log_get_level(uint8_t module_id)
{
	if (module_id < LOG_MOD_COUNT) {
		return log_runtime_levels[module_id];
	}

	return LOG_LEVEL_OFF;
}

#endif /* CONFIG_PRINTK */