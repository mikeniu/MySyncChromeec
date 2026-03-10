#ifndef MBEDTLS_NET_SERIAL_H
#define MBEDTLS_NET_SERIAL_H
/************************************************************************************

************************************************************************************/
#include "libusb.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        libusb_device_handle *handle;
        libusb_device **devs;
        libusb_context *ctx;
        uint8_t endpoint_in;
        uint8_t endpoint_out;
    } usb_context_t;

    typedef struct __attribute__((__packed__))
    {
        unsigned char header;
        unsigned short len;
        unsigned char cmd;
        // data//
        unsigned char tail[1];
    } ff_packet;

#define MBEDTLS_ERR_NET_SOCKET_FAILED -0x0042    /**< Failed to open a socket. */
#define MBEDTLS_ERR_NET_CONNECT_FAILED -0x0044   /**< The connection to the given server / port failed. */
#define MBEDTLS_ERR_NET_BIND_FAILED -0x0046      /**< Binding of the socket failed. */
#define MBEDTLS_ERR_NET_LISTEN_FAILED -0x0048    /**< Could not listen on the socket. */
#define MBEDTLS_ERR_NET_ACCEPT_FAILED -0x004A    /**< Could not accept the incoming connection. */
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C      /**< Reading information from the socket failed. */
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E      /**< Sending information through the socket failed. */
#define MBEDTLS_ERR_NET_CONN_RESET -0x0050       /**< Connection was reset by peer. */
#define MBEDTLS_ERR_NET_UNKNOWN_HOST -0x0052     /**< Failed to get an IP address for the given hostname. */
#define MBEDTLS_ERR_NET_BUFFER_TOO_SMALL -0x0043 /**< Buffer is too small to hold the data. */
#define MBEDTLS_ERR_NET_INVALID_CONTEXT -0x0045  /**< The context is invalid, eg because it was free()ed. */
#define MBEDTLS_ERR_NET_POLL_FAILED -0x0047      /**< Polling the net context failed. */
#define MBEDTLS_ERR_NET_BAD_INPUT_DATA -0x0049   /**< Input invalid. */

#if 0
enum   libusb_error 
{ 
   LIBUSB_SUCCESS = 0, 
   LIBUSB_ERROR_IO = -1,
   LIBUSB_ERROR_INVALID_PARAM = -2,
   LIBUSB_ERROR_ACCESS = -3, 
   LIBUSB_ERROR_NO_DEVICE = -4, 
   LIBUSB_ERROR_NOT_FOUND = -5, 
   LIBUSB_ERROR_BUSY = -6, 
   LIBUSB_ERROR_TIMEOUT = -7, 
   LIBUSB_ERROR_OVERFLOW = -8, 
   LIBUSB_ERROR_PIPE = -9, 
   LIBUSB_ERROR_INTERRUPTED = -10, 
   LIBUSB_ERROR_NO_MEM = -11, 
   LIBUSB_ERROR_NOT_SUPPORTED = -12, 
   LIBUSB_ERROR_OTHER = -99 
 }
#endif

    int mbedtls_usb_init(const uint32_t pid, const uint32_t vid);

    int mbedtls_usb_connect(const uint32_t pid, const uint32_t vid);

    int mbedtls_usb_reconnect(const uint32_t pid, const uint32_t vid);

    int mbedtls_usb_send(const uint8_t *buf, uint32_t len);

    int mbedtls_usb_recv(uint8_t *buf, uint32_t len);

    int mbedtls_usb_recv_timeout(uint8_t *buf, uint32_t len, uint32_t timeout);

    void mbedtls_usb_free(void);

    int mbedtls_usb_exist(uint16_t *pid, uint16_t *vid);
    
    int LIBUSB_CALL usb_remove_event_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // MBEDTLS_NET_SERIAL_H
