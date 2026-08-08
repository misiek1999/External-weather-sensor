#ifndef LOG_CONFIG_H_
#define LOG_CONFIG_H_

#include <stdint.h>

/*
 * Module registry for the compact logger (include/log.h, pulled in
 * automatically).
 *
 * List every module once here; each entry sets the compile-time default
 * level. From this single list the logger derives the module ids
 * (LOG_MOD_<TAG>), the per-module compile-time defaults
 * (LOG_DEFAULT_<TAG>) and the initial values of the runtime level table
 * (src/log.c).
 *
 * Example: MAIN logs everything up to INF. AHT10 is set to LOG_LEVEL_OFF,
 * which compiles all of the module's log calls out completely. Change it
 * here and rebuild to enable it.
 */
#define LOG_MODULES(_X) \
	_X(MAIN,  LOG_LEVEL_INF) \
	_X(AHT10, LOG_LEVEL_OFF)

/* Module ids used at run time (log_set_level / log_get_level). */
enum {
#define _X(_tag, _level) LOG_MOD_##_tag,
	LOG_MODULES(_X)
#undef _X
	LOG_MOD_COUNT
};

/* Compile-time default level per module (selected by LOG_MODULE in each TU). */
enum {
#define _X(_tag, _level) LOG_DEFAULT_##_tag = (_level),
	LOG_MODULES(_X)
#undef _X
};

#endif /* LOG_CONFIG_H_ */