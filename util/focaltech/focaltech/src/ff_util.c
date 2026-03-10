
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "ff_defs.h"
#include "ff_log.h"
#include "ff_util.h"

static const char log_level_char[] = {'A', 'V', 'D', 'I', 'W', 'E', 'U'};

void ff_util_hexdump(uint8_t mak, const char *info, const uint8_t *buf, uint32_t len)
{
    uint16_t i = 0;
    char tmp_buf[17] = {0};
    char *p = NULL;
    uint8_t *addr = (uint8_t *)buf;

    if (0 == mak)
    {
        return;
    }
    if (info)
    {
        printf("%s: length = %d [0x%X]\r\n", info, len, len);
    }
    if (len == 0)
    {
        return;
    }
    p = tmp_buf;
    printf("%p  ", addr);

    for (i = 0; i < len; i++)
    {
        printf("%02X ", buf[i]);
        if ((buf[i] >= 0x20) && (buf[i] < 0x7F))
        {
            *p++ = buf[i];
        }
        else
        {
            *p++ = '.';
        }
        if ((i + 1) % 16 == 0)
        {
            *p++ = 0; // string end
            printf("        | %s", tmp_buf);
            p = tmp_buf;

            printf("\r\n");

            if ((i + 1) < len)
            {
                addr += 16;
                printf("%p  ", addr);
            }
        }
        else if ((i + 1) % 8 == 0)
        {
            printf("- ");
        }
    }
    if (len % 16 != 0)
    {
        for (i = len % 16; i < 16; i++)
        {
            printf("   ");
            if (((i + 1) % 8 == 0) && ((i + 1) % 16 != 0))
            {
                printf("- ");
            }
        }
        *p++ = 0; // string end
        printf("        | %s", tmp_buf);
        printf("\r\n");
    }
}

uint64_t ff_util_rand64(void)
{
    return ((uint64_t)rand() << 32) | ((uint64_t)rand());
}

uint64_t ff_util_timestamp(bool timing)
{
    static uint64_t t0;
    struct timespec tp;
    uint64_t ts;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    ts = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;

    /* Timing. */
    if (!timing)
    {
        t0 = ts;
    }
    else
    {
        ts -= t0;
    }

    return ts;
}

void *ff_util_malloc(uint32_t size)
{
    return malloc(size);
}

void ff_util_free(void *buf)
{
    return free(buf);
}

void *ff_util_realloc(void *buf, uint32_t size)
{
    return realloc(buf, size);
}

int ff_util_sprintf(char *buf, const char *fmt, ...)
{
    int n = 0;
    va_list args;

    va_start(args, fmt);
    n = vsprintf(buf, fmt, args);
    va_end(args);

    return n;
}

static void ft_get_time_local(char *time_buff)
{
    char buff[32];
    struct timeval tv;
    struct tm timeinfo;

    gettimeofday(&tv,NULL);

    localtime_r(&tv.tv_sec,&timeinfo);

    strftime(buff,sizeof(buff),"%m-%d %H:%M:%S",&timeinfo);
    sprintf(time_buff,"%s.%03ld",buff,tv.tv_usec/1000);

}

int log_printf(log_level_t level, const char *tag, const char *fmt, ...)
{
#ifdef __DEBUG_MODE__
    
    char buf[LOG_BUF_SIZE];
    char time_buff[64];
    if (level >= __LOG_LEVEL__)
    {

        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, LOG_BUF_SIZE - 1, fmt, ap);
        va_end(ap);

        ft_get_time_local(time_buff);

        /*1.Write to console.*/
        if (level == LOG_LEVEL_ERR)
        {
            printf("\033[31m[%s] %c [%s] %s \033[0m\n", time_buff, log_level_char[level], tag, buf);
        } 
        else if (level == LOG_LEVEL_WRN)
        {
            printf("\033[33m[%s] %c [%s] %s \033[0m\n", time_buff, log_level_char[level], tag, buf);
        }
        else if (level == LOG_LEVEL_INF)
        {
            printf("\033[36m[%s] %c [%s] %s \033[0m\n", time_buff, log_level_char[level], tag, buf);
        }
        else
        {
            printf("[%s] %c [%s] %s\n", time_buff, log_level_char[level], tag, buf);
        }
    }

#endif
    return 0;
}
