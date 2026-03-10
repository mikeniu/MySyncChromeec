#ifndef FT_SG_IO_H
#define FT_SG_IO_H

#include <stdint.h>
#include <scsi/sg.h>

// USB Mass Storage Bulk-Only Transport magic
#define USB_MS_CBW_SIGNATURE 0x43425355
#define USB_MS_CSW_SIGNATURE 0x53425355
#define USB_MS_CBW_LENGTH 31
#define USB_MS_CSW_LENGTH 13

#define DIRECTION_NONE 0
#define DIRECTION_IN 1
#define DIRECTION_OUT 2

// error code
#define SG_IO_SUCCESS 0
#define SG_IO_CHECK_CONDITION 1
#define SG_IO_DRIVER_ERROR 2
#define SG_IO_TIMEOUT 3
#define SG_IO_BAD_TARGET 4

// BOT (Bulk-Only Transport)
struct bulk_cb_wrap
{
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t cmd[16];
} __attribute__((packed));

struct bulk_cs_wrap
{
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t status;
} __attribute__((packed));
;

int ft_sg_init();
int ft_sg_open(uint16_t vendor_id, uint16_t product_id);
int ft_sg_io(struct sg_io_hdr *io_hdr);
void ft_sg_close();



#endif
