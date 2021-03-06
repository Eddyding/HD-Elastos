/*
 * File      : mstorage.c
 *
 * COPYRIGHT (C) 2012, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-10-01     Yi Qiu       first version
 * 2012-11-25     Heyuanjie87  reduce the memory consumption
 * 2012-12-09     Heyuanjie87  change function and endpoint handler
 * 2013-07-25     Yi Qiu       update for USB CV test
 */

#include <hdElastosMantle.h>
#include <rtservice.h>
#include <rtdevice.h>
#include "mstorage.h"

#ifdef RT_USB_DEVICE_MSTORAGE

enum STAT
{
    STAT_CBW,
    STAT_CMD,
    STAT_CSW,
    STAT_RECEIVE,
    STAT_SEND,
};

typedef enum
{
    FIXED,
    COUNT,
    BLOCK_COUNT,
}CB_SIZE_TYPE;

typedef enum
{
    DIR_IN,
    DIR_OUT,
    DIR_NONE,
}CB_DIR;

typedef UInt32 (*cbw_handler)(ufunction_t func, ustorage_cbw_t cbw);

struct scsi_cmd
{
    UInt16 cmd;
    cbw_handler handler;
    UInt32 cmd_len;
    CB_SIZE_TYPE type;
    UInt32 data_size;
    CB_DIR dir;
};

struct mstorage
{
    struct ustorage_csw csw_response;
    uep_t ep_in;
    uep_t ep_out;
    int status;
    UInt32 cb_data_size;
    rt_device_t disk;
    UInt32 block;
    Int32 count;
    Int32 size;
    struct scsi_cmd* processing;
    struct rt_device_blk_geometry geometry;
};

static struct udevice_descriptor dev_desc =
{
    USB_DESC_LENGTH_DEVICE,     //bLength;
    USB_DESC_TYPE_DEVICE,       //type;
    USB_BCD_VERSION,            //bcdUSB;
    USB_CLASS_MASS_STORAGE,     //bDeviceClass;
    0x00,                       //bDeviceSubClass;
    0x00,                       //bDeviceProtocol;
    0x40,                       //bMaxPacketSize0;
    _VENDOR_ID,                 //idVendor;
    _PRODUCT_ID,                //idProduct;
    USB_BCD_DEVICE,             //bcdDevice;
    USB_STRING_MANU_INDEX,      //iManufacturer;
    USB_STRING_PRODUCT_INDEX,   //iProduct;
    USB_STRING_SERIAL_INDEX,    //iSerialNumber;
    USB_DYNAMIC,                //bNumConfigurations;
};

static struct usb_qualifier_descriptor dev_qualifier =
{
    sizeof(dev_qualifier),
    USB_DESC_TYPE_DEVICEQUALIFIER,
    0x0200,
    USB_CLASS_MASS_STORAGE,
    0x00,
    64,
    0x01,
    0,
};

const static struct umass_descriptor _mass_desc =
{
    USB_DESC_LENGTH_INTERFACE,  //bLength;
    USB_DESC_TYPE_INTERFACE,    //type;
    USB_DYNAMIC,                //bInterfaceNumber;
    0x00,                       //bAlternateSetting;
    0x02,                       //bNumEndpoints
    USB_CLASS_MASS_STORAGE,     //bInterfaceClass;
    0x06,                       //bInterfaceSubClass;
    0x50,                       //bInterfaceProtocol;
    0x00,                       //iInterface;

    USB_DESC_LENGTH_ENDPOINT,   //bLength;
    USB_DESC_TYPE_ENDPOINT,     //type;
    USB_DYNAMIC | USB_DIR_OUT,  //bEndpointAddress;
    USB_EP_ATTR_BULK,           //bmAttributes;
    0x40,                       //wMaxPacketSize;
    0x00,                       //bInterval;

    USB_DESC_LENGTH_ENDPOINT,   //bLength;
    USB_DESC_TYPE_ENDPOINT,     //type;
    USB_DYNAMIC | USB_DIR_IN,   //bEndpointAddress;
    USB_EP_ATTR_BULK,           //bmAttributes;
    0x40,                       //wMaxPacketSize;
    0x00,                       //bInterval;
};

const static char* _ustring[] =
{
    "Language",
    "RT-Thread Team.",
    "RTT Mass Storage",
    "320219198301",
    "Configuration",
    "Interface",
};

static UInt32 _test_unit_ready(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _request_sense(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _inquiry_cmd(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _allow_removal(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _start_stop(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _mode_sense_6(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _read_capacities(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _read_capacity(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _read_10(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _write_10(ufunction_t func, ustorage_cbw_t cbw);
static UInt32 _verify_10(ufunction_t func, ustorage_cbw_t cbw);

static struct scsi_cmd cmd_data[] =
{
    {SCSI_TEST_UNIT_READY, _test_unit_ready, 6,  FIXED,       0, DIR_NONE},
    {SCSI_REQUEST_SENSE,   _request_sense,   6,  COUNT,       0, DIR_IN},
    {SCSI_INQUIRY_CMD,     _inquiry_cmd,     6,  COUNT,       0, DIR_IN},
    {SCSI_ALLOW_REMOVAL,   _allow_removal,   6,  FIXED,       0, DIR_NONE},
    {SCSI_MODE_SENSE_6,    _mode_sense_6,    6,  COUNT,       0, DIR_IN},
    {SCSI_START_STOP,      _start_stop,      6,  FIXED,       0, DIR_NONE},
    {SCSI_READ_CAPACITIES, _read_capacities, 10, COUNT,       0, DIR_NONE},
    {SCSI_READ_CAPACITY,   _read_capacity,   10, FIXED,       8, DIR_IN},
    {SCSI_READ_10,         _read_10,         10, BLOCK_COUNT, 0, DIR_IN},
    {SCSI_WRITE_10,        _write_10,        10, BLOCK_COUNT, 0, DIR_OUT},
    {SCSI_VERIFY_10,       _verify_10,       10, FIXED,       0, DIR_NONE},
};

static void _send_status(ufunction_t func)
{
    struct mstorage *data;

    assert(func != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_send_status\n"));

    data = (struct mstorage*)func->user_data;
    data->ep_in->request.buffer = (UInt8*)&data->csw_response;
    data->ep_in->request.size = SIZEOF_CSW;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_CSW;
}

static UInt32 _test_unit_ready(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(func->device != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_test_unit_ready\n"));

    data = (struct mstorage*)func->user_data;
    data->csw_response.status = 0;

    return 0;
}

static UInt32 _allow_removal(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(func->device != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_allow_removal\n"));

    data = (struct mstorage*)func->user_data;
    data->csw_response.status = 0;

    return 0;
}

/**
 * This function will handle inquiry command request.
 *
 * @param func the usb function object.
 * @param cbw the command block wrapper.
 *
 * @return RT_EOK on successful.
 */

static UInt32 _inquiry_cmd(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;
    UInt8 *buf;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_inquiry_cmd\n"));

    data = (struct mstorage*)func->user_data;
    buf = data->ep_in->buffer;

    *(UInt32*)&buf[0] = 0x0 | (0x80 << 8);
    *(UInt32*)&buf[4] = 31;

    memset(&buf[8], 0x20, 28);
    memcpy(&buf[8], "RTT", 3);
    memcpy(&buf[16], "USB Disk", 8);

    data->cb_data_size = MIN(data->cb_data_size, SIZEOF_INQUIRY_CMD);
    data->ep_in->request.buffer = buf;
    data->ep_in->request.size = data->cb_data_size;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_CMD;

    return data->cb_data_size;
}

/**
 * This function will handle sense request.
 *
 * @param func the usb function object.
 * @param cbw the command block wrapper.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _request_sense(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;
    struct request_sense_data *buf;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_request_sense\n"));

    data = (struct mstorage*)func->user_data;
    buf = (struct request_sense_data *)data->ep_in->buffer;

    buf->ErrorCode = 0x70;
    buf->Valid = 0;
    buf->SenseKey = 2;
    buf->Information[0] = 0;
    buf->Information[1] = 0;
    buf->Information[2] = 0;
    buf->Information[3] = 0;
    buf->AdditionalSenseLength = 0x0a;
    buf->AdditionalSenseCode   = 0x3a;
    buf->AdditionalSenseCodeQualifier = 0;

    data->cb_data_size = MIN(data->cb_data_size, SIZEOF_REQUEST_SENSE);
    data->ep_in->request.buffer = (UInt8*)data->ep_in->buffer;
    data->ep_in->request.size = data->cb_data_size;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_CMD;

    return data->cb_data_size;
}

/**
 * This function will handle mode_sense_6 request.
 *
 * @param func the usb function object.
 * @param cbw the command block wrapper.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _mode_sense_6(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;
    UInt8 *buf;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_mode_sense_6\n"));

    data = (struct mstorage*)func->user_data;
    buf = data->ep_in->buffer;
    buf[0] = 3;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;

    data->cb_data_size = MIN(data->cb_data_size, SIZEOF_MODE_SENSE_6);
    data->ep_in->request.buffer = buf;
    data->ep_in->request.size = data->cb_data_size;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_CMD;

    return data->cb_data_size;
}

/**
 * This function will handle read_capacities request.
 *
 * @param func the usb function object.
 * @param cbw the command block wrapper.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _read_capacities(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;
    UInt8 *buf;
    UInt32 sector_count, sector_size;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_read_capacities\n"));

    data = (struct mstorage*)func->user_data;
    buf = data->ep_in->buffer;
    sector_count = data->geometry.sector_count;
    sector_size = data->geometry.bytes_per_sector;

    *(UInt32*)&buf[0] = 0x08000000;
    buf[4] = sector_count >> 24;
    buf[5] = 0xff & (sector_count >> 16);
    buf[6] = 0xff & (sector_count >> 8);
    buf[7] = 0xff & (sector_count);
    buf[8] = 0x02;
    buf[9] = 0xff & (sector_size >> 16);
    buf[10] = 0xff & (sector_size >> 8);
    buf[11] = 0xff & sector_size;

    data->cb_data_size = MIN(data->cb_data_size, SIZEOF_READ_CAPACITIES);
    data->ep_in->request.buffer = buf;
    data->ep_in->request.size = data->cb_data_size;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_CMD;

    return data->cb_data_size;
}

/**
 * This function will handle read_capacity request.
 *
 * @param func the usb function object.
 * @param cbw the command block wapper.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _read_capacity(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;

    UInt8 *buf;
    UInt32 sector_count, sector_size;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_read_capacity\n"));

    data = (struct mstorage*)func->user_data;
    buf = data->ep_in->buffer;
    sector_count = data->geometry.sector_count;
    sector_size = data->geometry.bytes_per_sector;

    buf[0] = sector_count >> 24;
    buf[1] = 0xff & (sector_count >> 16);
    buf[2] = 0xff & (sector_count >> 8);
    buf[3] = 0xff & (sector_count);
    buf[4] = 0x0;
    buf[5] = 0xff & (sector_size >> 16);
    buf[6] = 0xff & (sector_size >> 8);
    buf[7] = 0xff & sector_size;

    data->cb_data_size = MIN(data->cb_data_size, SIZEOF_READ_CAPACITY);
    data->ep_in->request.buffer = buf;
    data->ep_in->request.size = data->cb_data_size;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_CMD;

    return data->cb_data_size;
}

/**
 * This function will handle read_10 request.
 *
 * @param func the usb function object.
 * @param cbw the command block wrapper.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _read_10(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;
    UInt32 size;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    data = (struct mstorage*)func->user_data;
    data->block = cbw->cb[2]<<24 | cbw->cb[3]<<16 | cbw->cb[4]<<8  |
             cbw->cb[5]<<0;
    data->count = cbw->cb[7]<<8 | cbw->cb[8]<<0;

    assert(data->count < data->geometry.sector_count);

    data->csw_response.data_reside = data->cb_data_size;
    size = rt_device_read(data->disk, data->block, data->ep_in->buffer, 1);
    if(size == 0)
    {
        printf("read data error\n");
    }

    data->ep_in->request.buffer = data->ep_in->buffer;
    data->ep_in->request.size = data->geometry.bytes_per_sector;
    data->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
    data->status = STAT_SEND;

    return data->geometry.bytes_per_sector;
}

/**
 * This function will handle write_10 request.
 *
 * @param func the usb function object.
 * @param cbw the command block wrapper.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _write_10(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(cbw != NULL);

    data = (struct mstorage*)func->user_data;

    data->block = cbw->cb[2]<<24 | cbw->cb[3]<<16 | cbw->cb[4]<<8  |
             cbw->cb[5]<<0;
    data->count = cbw->cb[7]<<8 | cbw->cb[8];
    data->csw_response.data_reside = cbw->xfer_len;
    data->size = data->count * data->geometry.bytes_per_sector;

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_write_10 count 0x%x block 0x%x 0x%x\n",
                                data->count, data->block, data->geometry.sector_count));

    data->csw_response.data_reside = data->cb_data_size;

    data->ep_out->request.buffer = data->ep_out->buffer;
    data->ep_out->request.size = data->geometry.bytes_per_sector;
    data->ep_out->request.req_type = UIO_REQUEST_READ_FULL;
    rt_usbd_io_request(func->device, data->ep_out, &data->ep_out->request);
    data->status = STAT_RECEIVE;

    return data->geometry.bytes_per_sector;
}

/**
 * This function will handle verify_10 request.
 *
 * @param func the usb function object.
 *
 * @return RT_EOK on successful.
 */
static UInt32 _verify_10(ufunction_t func, ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(func->device != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_verify_10\n"));

    data = (struct mstorage*)func->user_data;
    data->csw_response.status = 0;

    return 0;
}

static UInt32 _start_stop(ufunction_t func,
    ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(func->device != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_start_stop\n"));

    data = (struct mstorage*)func->user_data;
    data->csw_response.status = 0;

    return 0;
}

static Int32 _ep_in_handler(ufunction_t func, UInt32 size)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(func->device != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_ep_in_handler\n"));

    data = (struct mstorage*)func->user_data;

    switch(data->status)
    {
    case STAT_CSW:
        if(data->ep_in->request.size != SIZEOF_CSW)
        {
            printf("Size of csw command error\n");
            rt_usbd_ep_set_stall(func->device, data->ep_in);
        }
        else
        {
            RT_DEBUG_LOG(RT_DEBUG_USB, ("return to cbw status\n"));
            data->ep_out->request.buffer = data->ep_out->buffer;
            data->ep_out->request.size = SIZEOF_CBW;
            data->ep_out->request.req_type = UIO_REQUEST_READ_FULL;
            rt_usbd_io_request(func->device, data->ep_out, &data->ep_out->request);
            data->status = STAT_CBW;
        }
        break;
     case STAT_CMD:
        if(data->csw_response.data_reside == 0xFF)
        {
            data->csw_response.data_reside = 0;
        }
        else
        {
            data->csw_response.data_reside -= data->ep_in->request.size;
            if(data->csw_response.data_reside != 0)
            {
                RT_DEBUG_LOG(RT_DEBUG_USB, ("data_reside %d, request %d\n",
                    data->csw_response.data_reside, data->ep_in->request.size));
                if(data->processing->dir == DIR_OUT)
                {
                    rt_usbd_ep_set_stall(func->device, data->ep_out);
                }
                else
                {
                    rt_usbd_ep_set_stall(func->device, data->ep_in);
                }
                data->csw_response.data_reside = 0;
            }
            }
        _send_status(func);
        break;
     case STAT_SEND:
        data->csw_response.data_reside -= data->ep_in->request.size;
        data->count--;
        data->block++;
        if(data->count > 0 && data->csw_response.data_reside > 0)
        {
            if(rt_device_read(data->disk, data->block, data->ep_in->buffer, 1) == 0)
            {
                printf("disk read error\n");
                rt_usbd_ep_set_stall(func->device, data->ep_in);
                return -RT_ERROR;
            }

            data->ep_in->request.buffer = data->ep_in->buffer;
            data->ep_in->request.size = data->geometry.bytes_per_sector;
            data->ep_in->request.req_type = UIO_REQUEST_WRITE;
            rt_usbd_io_request(func->device, data->ep_in, &data->ep_in->request);
        }
        else
        {
            _send_status(func);
        }
        break;
     }

     return RT_EOK;
}

#ifdef  MASS_CBW_DUMP
static void cbw_dump(struct ustorage_cbw* cbw)
{
    assert(cbw != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("signature 0x%x\n", cbw->signature));
    RT_DEBUG_LOG(RT_DEBUG_USB, ("tag 0x%x\n", cbw->tag));
    RT_DEBUG_LOG(RT_DEBUG_USB, ("xfer_len 0x%x\n", cbw->xfer_len));
    RT_DEBUG_LOG(RT_DEBUG_USB, ("dflags 0x%x\n", cbw->dflags));
    RT_DEBUG_LOG(RT_DEBUG_USB, ("lun 0x%x\n", cbw->lun));
    RT_DEBUG_LOG(RT_DEBUG_USB, ("cb_len 0x%x\n", cbw->cb_len));
    RT_DEBUG_LOG(RT_DEBUG_USB, ("cb[0] 0x%x\n", cbw->cb[0]));
}
#endif

static struct scsi_cmd* _find_cbw_command(UInt16 cmd)
{
    int i;

    for(i=0; i<sizeof(cmd_data)/sizeof(struct scsi_cmd); i++)
    {
        if(cmd_data[i].cmd == cmd)
            return &cmd_data[i];
    }

    return NULL;
}

static void _cb_len_calc(ufunction_t func, struct scsi_cmd* cmd,
    ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(cmd != NULL);
    assert(cbw != NULL);

    data = (struct mstorage*)func->user_data;
    if(cmd->cmd_len == 6)
    {
        switch(cmd->type)
        {
        case COUNT:
            data->cb_data_size = cbw->cb[4];
            break;
        case BLOCK_COUNT:
            data->cb_data_size = cbw->cb[4] * data->geometry.bytes_per_sector;
            break;
        case FIXED:
            data->cb_data_size = cmd->data_size;
            break;
        default:
            break;
        }
    }
    else if(cmd->cmd_len == 10)
    {
        switch(cmd->type)
        {
        case COUNT:
            data->cb_data_size = cbw->cb[7]<<8 | cbw->cb[8];
            break;
        case BLOCK_COUNT:
            data->cb_data_size = (cbw->cb[7]<<8 | cbw->cb[8]) *
                data->geometry.bytes_per_sector;
            break;
        case FIXED:
            data->cb_data_size = cmd->data_size;
            break;
        default:
            break;
        }
    }
    else
    {
        printf("cmd_len error %d\n", cmd->cmd_len);
    }
}

static rt_bool_t _cbw_verify(ufunction_t func, struct scsi_cmd* cmd,
    ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(cmd != NULL);
    assert(cbw != NULL);
    assert(func != NULL);

    data = (struct mstorage*)func->user_data;
    if(cmd->cmd_len != cbw->cb_len)
    {
        printf("cb_len error\n");
        cmd->cmd_len = cbw->cb_len;
    }

    if(cbw->xfer_len > 0 && data->cb_data_size == 0)
    {
        printf("xfer_len > 0 && data_size == 0\n");
        return RT_FALSE;
    }

    if(cbw->xfer_len == 0 && data->cb_data_size > 0)
    {
        printf("xfer_len == 0 && data_size > 0");
        return RT_FALSE;
    }

    if((cbw->dflags & USB_DIR_IN) && cmd->dir == DIR_OUT ||
        !(cbw->dflags & USB_DIR_IN) && cmd->dir == DIR_IN)
    {
        printf("dir error\n");
        return RT_FALSE;
    }

    if(cbw->xfer_len > data->cb_data_size)
    {
        printf("xfer_len > data_size\n");
        return RT_FALSE;
    }

    if(cbw->xfer_len < data->cb_data_size)
    {
        printf("xfer_len < data_size\n");
        data->cb_data_size = cbw->xfer_len;
        data->csw_response.status = 1;
    }

    return RT_TRUE;
}

static UInt32 _cbw_handler(ufunction_t func, struct scsi_cmd* cmd,
    ustorage_cbw_t cbw)
{
    struct mstorage *data;

    assert(func != NULL);
    assert(cbw != NULL);
    assert(cmd->handler != NULL);

    data = (struct mstorage*)func->user_data;
    data->processing = cmd;
    return cmd->handler(func, cbw);
}

/**
 * This function will handle mass storage bulk out endpoint request.
 *
 * @param func the usb function object.
 * @param size request size.
 *
 * @return RT_EOK.
 */
static Int32 _ep_out_handler(ufunction_t func, UInt32 size)
{
    struct mstorage *data;
    struct scsi_cmd* cmd;
    UInt32 len;
    struct ustorage_cbw* cbw;

    assert(func != NULL);
    assert(func->device != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("_ep_out_handler %d\n", size));

    data = (struct mstorage*)func->user_data;
    cbw = (struct ustorage_cbw*)data->ep_out->buffer;
    if(data->status == STAT_CBW)
    {
        /* dump cbw information */
        if(cbw->signature != CBW_SIGNATURE || size != SIZEOF_CBW)
        {
            goto exit;
        }

        data->csw_response.signature = CSW_SIGNATURE;
        data->csw_response.tag = cbw->tag;
        data->csw_response.data_reside = cbw->xfer_len;
        data->csw_response.status = 0;

        RT_DEBUG_LOG(RT_DEBUG_USB, ("ep_out reside %d\n", data->csw_response.data_reside));

        cmd = _find_cbw_command(cbw->cb[0]);
        if(cmd == NULL)
        {
            printf("can't find cbw command\n");
            goto exit;
        }

        _cb_len_calc(func, cmd, cbw);
        if(!_cbw_verify(func, cmd, cbw))
        {
            goto exit;
        }

        len = _cbw_handler(func, cmd, cbw);
        if(len == 0)
        {
            _send_status(func);
        }

        return RT_EOK;
    }
    else if(data->status == STAT_RECEIVE)
    {
        RT_DEBUG_LOG(RT_DEBUG_USB, ("\nwrite size %d block 0x%x oount 0x%x\n",
                                    size, data->block, data->size));

        data->size -= size;
        data->csw_response.data_reside -= size;

        rt_device_write(data->disk, data->block, data->ep_out->buffer, 1);

        if(data->csw_response.data_reside != 0)
        {
            data->ep_out->request.buffer = data->ep_out->buffer;
            data->ep_out->request.size = data->geometry.bytes_per_sector;
            data->ep_out->request.req_type = UIO_REQUEST_READ_FULL;
            rt_usbd_io_request(func->device, data->ep_out, &data->ep_out->request);
            data->block ++;
        }
        else
        {
            _send_status(func);
        }

        return RT_EOK;
    }

exit:
    if(data->csw_response.data_reside)
    {
        if(cbw->dflags & USB_DIR_IN)
        {
            rt_usbd_ep_set_stall(func->device, data->ep_in);
        }
        else
        {
            rt_usbd_ep_set_stall(func->device, data->ep_in);
            rt_usbd_ep_set_stall(func->device, data->ep_out);
        }
    }
    data->csw_response.status = 1;
    _send_status(func);

    return -RT_ERROR;
}

/**
 * This function will handle mass storage interface request.
 *
 * @param func the usb function object.
 * @param setup the setup request.
 *
 * @return RT_EOK on successful.
 */
static Int32 _interface_handler(ufunction_t func, ureq_t setup)
{
    UInt8 lun = 0;

    assert(func != NULL);
    assert(func->device != NULL);
    assert(setup != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("mstorage_interface_handler\n"));

    switch(setup->request)
    {
    case USBREQ_GET_MAX_LUN:

        RT_DEBUG_LOG(RT_DEBUG_USB, ("USBREQ_GET_MAX_LUN\n"));

        if(setup->value || setup->length != 1)
        {
            rt_usbd_ep0_set_stall(func->device);
        }
        else
        {
            rt_usbd_ep0_write(func->device, &lun, 1);
        }
        break;
    case USBREQ_MASS_STORAGE_RESET:

        RT_DEBUG_LOG(RT_DEBUG_USB, ("USBREQ_MASS_STORAGE_RESET\n"));

        if(setup->value || setup->length != 0)
        {
            rt_usbd_ep0_set_stall(func->device);
        }
        else
        {
            rt_usbd_ep0_write(func->device, NULL, 0);
        }
        break;
    default:
        printf("unknown interface request\n");
        break;
    }

    return RT_EOK;
}

/**
 * This function will run mass storage function, it will be called on handle set configuration request.
 *
 * @param func the usb function object.
 *
 * @return RT_EOK on successful.
 */
static Int32 _function_enable(ufunction_t func)
{
    struct mstorage *data;
    assert(func != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("Mass storage function enabled\n"));
    data = (struct mstorage*)func->user_data;

    data->disk = rt_device_find(RT_USB_MSTORAGE_DISK_NAME);
    if(data->disk == NULL)
    {
        printf("no data->disk named %s\n", RT_USB_MSTORAGE_DISK_NAME);
        return -RT_ERROR;
    }

    if(rt_device_open(data->disk, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        printf("disk open error\n");
        return -RT_ERROR;
    }

    if(rt_device_control(data->disk, RT_DEVICE_CTRL_BLK_GETGEOME,
        (void*)&data->geometry) != RT_EOK)
    {
        printf("get disk info error\n");
        return -RT_ERROR;
    }

    data->ep_in->buffer = (UInt8*)malloc(data->geometry.bytes_per_sector);
    if(data->ep_in->buffer == NULL)
    {
        printf("no memory\n");
        return -RT_ENOMEM;
    }
    data->ep_out->buffer = (UInt8*)malloc(data->geometry.bytes_per_sector);
    if(data->ep_out->buffer == NULL)
    {
        rt_free(data->ep_in->buffer);
        printf("no memory\n");
        return -RT_ENOMEM;
    }

    /* prepare to read CBW request */
    data->ep_out->request.buffer = data->ep_out->buffer;
    data->ep_out->request.size = SIZEOF_CBW;
    data->ep_out->request.req_type = UIO_REQUEST_READ_FULL;
    rt_usbd_io_request(func->device, data->ep_out, &data->ep_out->request);

    return RT_EOK;
}

/**
 * This function will stop mass storage function, it will be called on handle set configuration request.
 *
 * @param device the usb device object.
 *
 * @return RT_EOK on successful.
 */
static Int32 _function_disable(ufunction_t func)
{
    struct mstorage *data;
    assert(func != NULL);

    RT_DEBUG_LOG(RT_DEBUG_USB, ("Mass storage function disabled\n"));

    data = (struct mstorage*)func->user_data;
    if(data->ep_in->buffer != NULL)
    {
        rt_free(data->ep_in->buffer);
        data->ep_in->buffer = NULL;
    }

    if(data->ep_out->buffer != NULL)
    {
        rt_free(data->ep_out->buffer);
        data->ep_out->buffer = NULL;
    }

    data->status = STAT_CBW;

    return RT_EOK;
}

static struct ufunction_ops ops =
{
    _function_enable,
    _function_disable,
    NULL,
};

/**
 * This function will create a mass storage function instance.
 *
 * @param device the usb device object.
 *
 * @return RT_EOK on successful.
 */
ufunction_t rt_usbd_function_mstorage_create(udevice_t device)
{
    uintf_t intf;
    struct mstorage *data;
    ufunction_t func;
    ualtsetting_t setting;
    umass_desc_t mass_desc;

    /* parameter check */
    assert(device != NULL);

    /* set usb device string description */
    rt_usbd_device_set_string(device, _ustring);

    /* create a mass storage function */
    func = rt_usbd_function_new(device, &dev_desc, &ops);
    device->dev_qualifier = &dev_qualifier;

    /* allocate memory for mass storage function data */
    data = (struct mstorage*)malloc(sizeof(struct mstorage));
    memset(data, 0, sizeof(struct mstorage));
    func->user_data = (void*)data;

    /* create an interface object */
    intf = rt_usbd_interface_new(device, _interface_handler);

    /* create an alternate setting object */
    setting = rt_usbd_altsetting_new(sizeof(struct umass_descriptor));

    /* config desc in alternate setting */
    rt_usbd_altsetting_config_descriptor(setting, &_mass_desc, 0);

    /* create a bulk out and a bulk in endpoint */
    mass_desc = (umass_desc_t)setting->desc;
    data->ep_in = rt_usbd_endpoint_new(&mass_desc->ep_in_desc, _ep_in_handler);
    data->ep_out = rt_usbd_endpoint_new(&mass_desc->ep_out_desc, _ep_out_handler);

    /* add the bulk out and bulk in endpoint to the alternate setting */
    rt_usbd_altsetting_add_endpoint(setting, data->ep_out);
    rt_usbd_altsetting_add_endpoint(setting, data->ep_in);

    /* add the alternate setting to the interface, then set default setting */
    rt_usbd_interface_add_altsetting(intf, setting);
    rt_usbd_set_altsetting(intf, 0);

    /* add the interface to the mass storage function */
    rt_usbd_function_add_interface(func, intf);

    return func;
}

#endif
