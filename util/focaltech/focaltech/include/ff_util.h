
#ifndef __FF_UTIL_H__
#define __FF_UTIL_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Dump the memory content to logcat like 'hexdump -C'.
     *
     * @params
     *  buf: Memory buffer.
     *  len: Buffer length.
     */
    void ff_util_hexdump(uint8_t mak, const char *info, const uint8_t *buf, uint32_t len);

    /*
     * Generate a 64-bits random number.
     *
     * @return
     *  The 64-bits random number.
     */
    uint64_t ff_util_rand64(void);

    /*
     * Retrieve the timestamp since the most recent system boot.
     * Or timing (Do *NOT* use it nested) in milliseconds.
     *
     * @params
     *  timing: =false, begin the timing. =true, end the timing.
     *
     * @return
     *  The 64-bits timestamp (or elapse if $timing=true) in milliseconds.
     */
    uint64_t ff_util_timestamp(bool timing);

    /*
     * Wait or sleep in milliseconds.
     *
     * @params
     *  ms: Numbers of milliseconds to wait/sleep.
     */
    void ff_util_msleep(uint32_t ms);

    /*
     * Dynamic memory management in heap.
     *
     * @params
     *  buf : Pointer to the alloc-ed/alloc-ing memory.
     *  size: Memory size to/of alloc-ing/alloc-ed.
     *
     * @return
     *  Pointer to the alloc-ed memory or NULL if fails.
     */
    void *ff_util_malloc(uint32_t size);
    void ff_util_free(void *buf);
    void *ff_util_realloc(void *buf, uint32_t size);

    /*
     * Common sprintf interface like the POSIX 'sprintf'.
     */
    int ff_util_sprintf(char *buf, const char *fmt, ...);

    /**
     * @brief Calculate 8-bits BCC over the given input $data.
     *
     * @param[in]  data: Pointer to the data buffer.
     * @param[in]  dlen: Data length to calculate.
     * @return	The calculated BCC-8 value.
     *
     */
    uint8_t ff_util_bcc(const void *data, uint32_t dlen);

    /**
     * @brief Calculate 8/16/32-bits CRC over the given input $data.
     *
     * @param[in]  data: Pointer to the data buffer.
     * @param[in]  dlen: Data length to calculate.
     * @param[in]  init: Previous calculated CRC value.
     * @return	The calculated CRC-8/16/32 value.
     *
     */
    uint8_t ff_util_crc_8(const void *data, uint32_t dlen, uint8_t init);
    uint16_t ff_util_crc_16(const void *data, uint32_t dlen, uint16_t init);
    uint32_t ff_util_crc_32(const void *data, uint32_t dlen, uint32_t init);

    /**
     * @brief
     *
     * @param[in]  x: value to normalize.
     * @param[in]  in_min: min value of original data.
     * @param[in]  in_max: max value of original data.
     * @param[in]  out_min: normalized min data.
     * @param[in]  out_max: normalized max data.
     * @return	normalized value.
     *
     */
    int ff_util_map(int x, int in_min, int in_max, int out_min, int out_max);

#ifdef __cplusplus
}
#endif

#endif //__DUMP_H__
