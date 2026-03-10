/**
 * log.h
 *
 * Author: wanmingming@focaltech-electronics.com
 *
 **/

#ifndef __FF_LOG_H__
#define __FF_LOG_H__

#include <string.h> // strrchr(..)

#define __DEBUG_MODE__

/**
 * The default LOG_TAG is 'focaltech', using these two lines to define newtag.
 * # undef LOG_TAG
 * #define LOG_TAG "newtag"
 */
#undef LOG_TAG
#define LOG_TAG "focaltech"

#define LOG_BUF_SIZE 1024

/**
 * Log level can be used in 'logcat <tag>[:priority]', and also be
 * used in output control while '__EARLY_LOG_LEVEL' is defined.
 */
typedef enum
{
    LOG_LEVEL_ALL = 0,
    LOG_LEVEL_VBS = 1, /* Verbose */
    LOG_LEVEL_DBG = 2, /* Debug   */
    LOG_LEVEL_INF = 3, /* Info    */
    LOG_LEVEL_WRN = 4, /* Warning */
    LOG_LEVEL_ERR = 5, /* Error   */
} log_level_t;

/**
 * __EARLY_LOG_LEVEL can be defined as compilation option.
 * default level is LOG_LEVEL_ALL(all the logs will be output).
 */
#ifndef __LOG_LEVEL__
#define __LOG_LEVEL__ LOG_LEVEL_DBG
#endif

/* Log level can be runtime configurable. */

/* Human readable names. */
extern const char *g_log_level_names[];

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Logging API. Do NOT use it directly but LOG* instead.
     *
     * @params
     *  level: Logging level for logcat.
     *  tag  : Logging tag for logcat.
     *  fmt  : See POSIX printf(..).
     *  ...  : See POSIX printf(..).
     *
     * @return
     *  The number of characters printed, or a negative value if there
     *  was an output error.
     */
    int log_printf(log_level_t level, const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

/**
 * Using the following five macros for conveniently logging.
 */
#define FF_LOGV(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (__LOG_LEVEL__ <= LOG_LEVEL_VBS)                                                                            \
        {                                                                                                              \
            log_printf(LOG_LEVEL_VBS, LOG_TAG, __VA_ARGS__);                                                           \
        };                                                                                                             \
    } while (0)

#define FF_LOGD(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (__LOG_LEVEL__ <= LOG_LEVEL_DBG)                                                                            \
        {                                                                                                              \
            log_printf(LOG_LEVEL_DBG, LOG_TAG, __VA_ARGS__);                                                           \
        };                                                                                                             \
    } while (0)

#define FF_LOGI(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (__LOG_LEVEL__ <= LOG_LEVEL_INF)                                                                            \
        {                                                                                                              \
            log_printf(LOG_LEVEL_INF, LOG_TAG, __VA_ARGS__);                                                           \
        };                                                                                                             \
    } while (0)

#define FF_LOGW(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (__LOG_LEVEL__ <= LOG_LEVEL_WRN)                                                                            \
        {                                                                                                              \
            log_printf(LOG_LEVEL_WRN, LOG_TAG, __VA_ARGS__);                                                           \
        };                                                                                                             \
    } while (0)

#define FF_LOGE(format, ...)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (__LOG_LEVEL__ <= LOG_LEVEL_ERR)                                                                            \
        {                                                                                                              \
            const char *__fname__ = strrchr(__FILE__, '/');                                                            \
            if (!__fname__)                                                                                            \
                __fname__ = strrchr(__FILE__, '\\');                                                                   \
            if (!__fname__)                                                                                            \
                __fname__ = __FILE__;                                                                                  \
            else                                                                                                       \
                __fname__ += 1;                                                                                        \
            log_printf(LOG_LEVEL_ERR, LOG_TAG, "error at %s[%s:%d]: " format, __FUNCTION__, __fname__, __LINE__,       \
                       ##__VA_ARGS__);                                                                                 \
        };                                                                                                             \
    } while (0)

#endif /* __BASE_LOGGING_H__ */
