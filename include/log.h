#ifndef LOG_H_
#define LOG_H_

#include <kernel.h>
#include <sys/printk.h>

/*
 * Compact embedded logging helper. Emits one line per record:
 *
 *   E: 13014 [30951][MAIN]<main> main.c:134- Angle: 9.19
 *   ^   ^      ^      ^     ^       ^          ^
 *   |   |      |      |     |       |          +- message
 *   |   |      |      |     |       +- file:line
 *   |   |      |      |     +- calling thread name ("?" if not available)
 *   |   |      |      +- module tag (LOG_MODULE)
 *   |   |      +- milliseconds since boot (k_uptime_get)
 *   |   +- monotonically rising record number (per boot, per module)
 *   +- level: F / C / E / W / N / I / D / T
 *
 * Full level set (higher = more verbose), also used as the per-module
 * compile-time filter:
 *
 *   OFF(0) FATAL(1) CRIT(2) ERR(3) WRN(4) NOTICE(5) INF(6) DBG(7) TRACE(8)
 *
 * All modules are registered once in log_config.h:
 *
 *   #define LOG_MODULES(_X) \
 *       _X(MAIN,  LOG_LEVEL_INF) \
 *       _X(AHT10, LOG_LEVEL_WRN)
 *
 * A module declares itself before including this file:
 *
 *   #define LOG_MODULE MAIN
 *   #include "log.h"
 *
 * Filtering happens at two levels:
 *
 *  - compile time: records below the module's default level (from
 *    log_config.h, or a local #define LOG_LEVEL override) are eliminated by
 *    the compiler. LOG_LEVEL_OFF compiles all of the module's log calls
 *    out entirely - zero flash, zero runtime cost.
 *  - run time: log_set_level(LOG_MOD_MAIN, LOG_LEVEL_WRN) (see src/log.c)
 *    changes the level of a module while the system runs. It costs one
 *    array lookup per record; disabled modules still keep their strings in
 *    flash, so leave unused modules at LOG_LEVEL_OFF.
 *
 * With CONFIG_PRINTK disabled every macro expands to nothing and no code or
 * data is emitted at all. The message is formatted synchronously, so stack
 * buffers can be passed directly to %s (no strdup / ring buffers involved).
 * The usual printk() format limitations apply (no %f).
 */

#ifndef LOG_LEVEL_OFF
#define LOG_LEVEL_OFF    0  /* excludes all log records */
#endif
#ifndef LOG_LEVEL_FATAL
#define LOG_LEVEL_FATAL  1
#endif
#ifndef LOG_LEVEL_CRIT
#define LOG_LEVEL_CRIT   2
#endif
#ifndef LOG_LEVEL_ERR
#define LOG_LEVEL_ERR    3
#endif
#ifndef LOG_LEVEL_WRN
#define LOG_LEVEL_WRN    4
#endif
#ifndef LOG_LEVEL_NOTICE
#define LOG_LEVEL_NOTICE 5
#endif
#ifndef LOG_LEVEL_INF
#define LOG_LEVEL_INF    6
#endif
#ifndef LOG_LEVEL_DBG
#define LOG_LEVEL_DBG    7
#endif
#ifndef LOG_LEVEL_TRACE
#define LOG_LEVEL_TRACE  8
#endif

/* Module registry (ids, compile-time defaults, runtime table sizing). */
#include "log_config.h"

/* Turn a module token (MAIN) into its tag ("MAIN"), id and default level. */
#define LOG_TAG_STR_2(_mod) #_mod
#define LOG_TAG_STR(_mod) LOG_TAG_STR_2(_mod)
#define LOG_MODULE_ID_OF_2(_mod) LOG_MOD_##_mod
#define LOG_MODULE_ID_OF(_mod) LOG_MODULE_ID_OF_2(_mod)
#define LOG_LEVEL_DEFAULT_2(_mod) LOG_DEFAULT_##_mod
#define LOG_LEVEL_DEFAULT(_mod) LOG_LEVEL_DEFAULT_2(_mod)

#ifndef LOG_TAG
#ifdef LOG_MODULE
#define LOG_TAG LOG_TAG_STR(LOG_MODULE)
#else
#define LOG_TAG "APP"
#endif
#endif

#ifndef LOG_LEVEL
#ifdef LOG_MODULE
#define LOG_LEVEL LOG_LEVEL_DEFAULT(LOG_MODULE)
#else
#define LOG_LEVEL LOG_LEVEL_INF
#endif
#endif

#if defined(CONFIG_PRINTK) && CONFIG_PRINTK

static uint32_t log_count __attribute__((unused));

extern uint8_t log_runtime_levels[];

static inline const char *log_basename(const char *path)
{
	const char *last = path;

	for (const char *p = path; *p != '\0'; p++) {
		if (*p == '/' || *p == '\\') {
			last = p + 1;
		}
	}

	return last;
}

static inline const char *log_thread(void)
{
#ifdef CONFIG_THREAD_NAME
	const char *name = k_thread_name_get(k_current_get());

	return name != NULL ? name : "?";
#else
	return "?";
#endif
}

#if defined(LOG_MODULE)
#define LOG_MODULE_ID   LOG_MODULE_ID_OF(LOG_MODULE)
#define LOG_RUNTIME_ON(_level) ((_level) <= log_runtime_levels[LOG_MODULE_ID])
#else
#define LOG_RUNTIME_ON(_level) 1
#endif

#define LOG_IMPL(_level_char, _level, _fmt, ...)                                \
	do {                                                                        \
		if ((_level) <= (LOG_LEVEL) && LOG_RUNTIME_ON(_level)) {               \
			printk("%c: %u [%u][%s]<%s> %s:%u- " _fmt "\n",                  \
			       (_level_char), ++log_count,                               \
			       (uint32_t)k_uptime_get(), LOG_TAG,                        \
			       log_thread(), log_basename(__FILE__),                     \
			       (unsigned)__LINE__ , ##__VA_ARGS__);                      \
		}                                                                       \
	} while (0)

#define LOG_F(...) LOG_IMPL('F', LOG_LEVEL_FATAL, __VA_ARGS__)
#define LOG_C(...) LOG_IMPL('C', LOG_LEVEL_CRIT, __VA_ARGS__)
#define LOG_E(...) LOG_IMPL('E', LOG_LEVEL_ERR, __VA_ARGS__)
#define LOG_W(...) LOG_IMPL('W', LOG_LEVEL_WRN, __VA_ARGS__)
#define LOG_N(...) LOG_IMPL('N', LOG_LEVEL_NOTICE, __VA_ARGS__)
#define LOG_I(...) LOG_IMPL('I', LOG_LEVEL_INF, __VA_ARGS__)
#define LOG_D(...) LOG_IMPL('D', LOG_LEVEL_DBG, __VA_ARGS__)
#define LOG_T(...) LOG_IMPL('T', LOG_LEVEL_TRACE, __VA_ARGS__)

#else /* !CONFIG_PRINTK */

#define LOG_F(...) do { } while (0)
#define LOG_C(...) do { } while (0)
#define LOG_E(...) do { } while (0)
#define LOG_W(...) do { } while (0)
#define LOG_N(...) do { } while (0)
#define LOG_I(...) do { } while (0)
#define LOG_D(...) do { } while (0)
#define LOG_T(...) do { } while (0)

#endif /* CONFIG_PRINTK */

#endif /* LOG_H_ */