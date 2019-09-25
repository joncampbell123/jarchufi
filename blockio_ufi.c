/* blockio code that uses libusb and the Control/Bulk/Interrupt protocol
 * and the UFI command set to read USB floppy drives
 *
 * It's nice to know that for UFI they borrowed a lot from SCSI... :) */

#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
#include <sys/ioctl.h>
#include <asm/fcntl.h>
#include <linux/fs.h>
#include <linux/cdrom.h>
#include <scsi/sg.h>
#include <signal.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "blockio.h"
#include "bitchin.h"
#include "config.h"
#include <usb.h>

unsigned int BEWORD(unsigned char *x)
{
	unsigned int N;

	N = (x[0] << 8) | x[1];
	return N;
}

static int					is_open = 0;
static struct usb_config_descriptor*		dev_cfg = NULL;
static struct usb_interface*			dev_if = NULL;
static struct usb_interface_descriptor*		dev_ifd = NULL;
static struct usb_dev_handle*			dev_handle = NULL;
static int					dev_ep_data_in = -1;	/* bulk input endpoint */
static int					dev_ep_data_out = -1;	/* bulk output endpoint */
static int					dev_ep_int_in = -1;	/* interrupt intput endpoint */
static unsigned int				dev_media_llba = 0;
static unsigned int				dev_media_blen = 0;

static unsigned char ufi_inquiry[] =
	{0x12,0x00,0x00,0x00,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static unsigned char ufi_send_diagnostic[] =
	{0x1D,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static unsigned char ufi_start_motor[] =
	{0x1B,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static unsigned char ufi_stop_motor[] =
	{0x1B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static unsigned char ufi_test_ready[] =
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static unsigned char ufi_sense_data[] =
	{0x03,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static unsigned char ufi_readcapac[] =
	{0x25,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

int ufi_sendcmd(unsigned char *cmd,int sz)
{
	return usb_control_msg(dev_handle,USB_TYPE_CLASS|USB_RECIP_INTERFACE,USB_REQ_GET_STATUS,0,0,
		cmd,sz,10000);
}

unsigned char					ufi_bulk_in_data_buffer[1024];
int						ufi_bulk_in_data_buffer_sz;
unsigned char					ufi_int_in_stat[2];
int						ufi_int_in_stat_sz;

int ufi_read_stat()
{
	ufi_int_in_stat_sz = usb_interrupt_read(dev_handle,
		dev_ep_int_in,ufi_int_in_stat,2,10000);

	if (ufi_int_in_stat_sz < 2)
		return -1;

	return 0;
}

int ufi_read_data(unsigned char *buffer,int max)
{
	return usb_bulk_read(dev_handle,dev_ep_data_in,buffer,max,10000);
}

int ufi_response(int expecting_data)
{
	if (expecting_data) {
/* Wait a minute.... shouldn't we read the status FIRST and THEN read the data?!?
 * This order works but the other order fails?!?! */
		ufi_bulk_in_data_buffer_sz = ufi_read_data(
			ufi_bulk_in_data_buffer,sizeof(ufi_bulk_in_data_buffer));

		if (ufi_bulk_in_data_buffer_sz < 0)
			return -1;
	}

/* then read status from interrupt endpoint */
	return ufi_read_stat();
}

static char *YesNo[] = {"No","Yes"};

void bitch_INQ_response()
{
	unsigned char *inq = (unsigned char*)ufi_bulk_in_data_buffer;
	char tmp[21];

	bitch(BITCHINFO,"......INQUIRY RESPONSE STATUS 0x%02X 0x%02X",ufi_int_in_stat[0],ufi_int_in_stat[1]);

	bitch(BITCHINFO,"......Peripheral device type:   0x%02X",inq[0] & 0x1F);
	bitch(BITCHINFO,"......Removable media:          %s",YesNo[inq[1]>>7]);
	bitch(BITCHINFO,"......ISO version:              %u",inq[2]>>6);
	bitch(BITCHINFO,"......ECMA version:             %u",(inq[2]>>3)&7);
	bitch(BITCHINFO,"......ANSI version:             %u",inq[2]&7);
	bitch(BITCHINFO,"......Response data format:     %u",inq[3]&15);
	/* vendor info */
	memcpy(tmp,inq +  8,8); tmp[8] = 0;
	bitch(BITCHINFO,"......Vendor:                   %s",tmp);
	/* product id */
	memcpy(tmp,inq + 16,16); tmp[16] = 0;
	bitch(BITCHINFO,"......Product ID:               %s",tmp);
	/* product revision */
	memcpy(tmp,inq + 32,4); tmp[4] = 0;
	bitch(BITCHINFO,"......Product revision:         %s",tmp);
}

static int ufi_status()
{
	return (int)(ufi_int_in_stat[1]&3);
}

/* use this after ufi_sense() */
static int ufi_sense_key()
{
	return (int)(ufi_bulk_in_data_buffer[1]);
}

int ufi_sense()
{
	if (ufi_sendcmd(ufi_sense_data,sizeof(ufi_sense_data)) < 0) {
		bitch(BITCHWARNING,"ufi_sense(): unable to send SENSE DATA command");
		return -1;
	}
	if (ufi_response(1) < 0) {
		bitch(BITCHWARNING,"ufi_sense(): no response");
		return -1;
	}
	if (ufi_status() != 0) {
		bitch(BITCHWARNING,"ufi_sense(): status = %u",ufi_status());
		return -1;
	}

	return 0;
}

int ufi_testready()
{
	if (ufi_sendcmd(ufi_test_ready,sizeof(ufi_test_ready)) < 0) {
		bitch(BITCHWARNING,"ufi_testready(): unable to send TEST UNIT READY command");
		return -1;
	}
	if (ufi_response(0) < 0) {
		bitch(BITCHWARNING,"ufi_testready(): no response");
		return -1;
	}
	if (ufi_status() != 0) {
		bitch(BITCHWARNING,"ufi_testready(): status = %u",ufi_status());
		return -1;
	}

	return 0;
}

int ufi_read(unsigned int LBA,unsigned int blocks,unsigned char *buffer,int bufsiz)
{
	unsigned char cmd[12],*fence,*ob = buffer;
	int sz,snext;

	cmd[ 0] = 0xA8;
	cmd[ 1] = 0x00;
	cmd[ 2] = LBA >> 24;
	cmd[ 3] = LBA >> 16;
	cmd[ 4] = LBA >> 8;
	cmd[ 5] = LBA;
	cmd[ 6] = blocks >> 24;
	cmd[ 7] = blocks >> 16;
	cmd[ 8] = blocks >> 8;
	cmd[ 9] = blocks;
	cmd[10] = 0x00;
	cmd[11] = 0x00;
	if (ufi_sendcmd(cmd,12) < 0) {
		bitch(BITCHWARNING,"ufi_read(): unable to send READ command");
		return -1;
	}

	fence = buffer + bufsiz;
	do {
		snext = (int)(fence - buffer);
		if (snext <= 0) break;

		sz = ufi_read_data(buffer,snext);
		if (sz > 0) {
			buffer += sz;
		}
		else {
			if (ufi_read_stat() < 0) {
				bitch(BITCHWARNING,"ufi_read(): read finished but no status?");
				return -1;
			}
		}
	} while (sz > 0);

	if (ufi_sense() < 0) {
		bitch(BITCHWARNING,"ufi_read(): unable to get sense data");
		return -1;
	}

	if (ufi_sense_key() != 0)
		bitch(BITCHWARNING,"ufi_read(): error sense key 0x%02X",ufi_sense_key());

	return (int)(buffer - ob);
}

/* read and display the contents of the Flexible Disk Page */
int ufi_msfdp()
{
	unsigned char *inq = (unsigned char*)ufi_bulk_in_data_buffer;
	unsigned char cmd[12],dat[32];
	int N;

	cmd[ 0] = 0x5A;
	cmd[ 1] = 0x00;
	cmd[ 2] = 0x05;	/* page 0x05 Flexible Disk Parameters */
	cmd[ 3] = 0x00;
	cmd[ 4] = 0x00;
	cmd[ 5] = 0x00;
	cmd[ 6] = 0x00;
	cmd[ 7] = sizeof(ufi_bulk_in_data_buffer) >> 8;
	cmd[ 8] = sizeof(ufi_bulk_in_data_buffer);
	cmd[ 9] = 0x00;
	cmd[10] = 0x00;
	cmd[11] = 0x00;

	if (ufi_sendcmd(cmd,12) < 0) {
		bitch(BITCHWARNING,"ufi_msfdp(): unable to send MODE SENSE");
		return -1;
	}
	if (ufi_response(1) < 0) {
		bitch(BITCHWARNING,"ufi_msfdp(): no response");
		return -1;
	}
	if (ufi_status() != 0) {
		bitch(BITCHWARNING,"ufi_msfdp(): status = %u",ufi_status());
		return -1;
	}
	if (ufi_bulk_in_data_buffer_sz < 40) {
		bitch(BITCHWARNING,"ufi_msfdp(): only %u bytes returned, not enough",ufi_bulk_in_data_buffer_sz);
		return -1;
	}

	bitch(BITCHINFO,"Media type is 0x%02X",inq[2]);
	N = BEWORD(inq);
	if (N < 32) {
		bitch(BITCHWARNING,"ufi_msfdp(): mode data length too small, only %u",N);
		return -1;
	}

	/* skip ahead to pages */
	inq += 8;

	if (inq[0] != 0x05 || inq[1] < 0x1E) {
		bitch(BITCHWARNING,"ufi_msfdp(): got response to MODE SENSE for page 5 but didn't get page 5");
		return -1;
	}

	memcpy(dat,inq,32);
	if (ufi_sense() < 0) {
		bitch(BITCHWARNING,"ufi_msfdp(): unable to get sense data");
		return -1;
	}
	if (ufi_sense_key() != 0) {
		bitch(BITCHWARNING,"ufi_msfdp(): didn't get sense data");
		return -1;
	}

	bitch(BITCHINFO,"Transfer rate:                 %u kbits/sec",BEWORD(dat +  2));
	bitch(BITCHINFO,"Number of heads:               %u",                 dat[   4]);
	bitch(BITCHINFO,"Sectors per track:             %u",                 dat[   5]);
	bitch(BITCHINFO,"Data bytes/sector:             %u",          BEWORD(dat +  6));
	bitch(BITCHINFO,"Cylinder count:                %u",          BEWORD(dat +  8));

	bitch(BITCHINFO,"Motor on delay:                %u.%01u seconds",dat[19]/10,dat[19]%10);
	bitch(BITCHINFO,"Motor off delay:               %u.%01u seconds",dat[20]/10,dat[20]%10);
	bitch(BITCHINFO,"Media rotation rate:           %u rpm",      BEWORD(dat + 28));
	return 0;
}

/* returns *llba = last logical block address
 *         *blen = block length in bytes         */
int ufi_read_capacity(unsigned int *llba,unsigned int *blen)
{
	unsigned char *inq = (unsigned char*)ufi_bulk_in_data_buffer;

	*llba = 0;
	*blen = 0;

	if (ufi_sendcmd(ufi_readcapac,sizeof(ufi_readcapac)) < 0) {
		bitch(BITCHWARNING,"ufi_readcapacity(): unable to send READ CAPACITY");
		return -1;
	}
	if (ufi_response(1) < 0) {
		bitch(BITCHWARNING,"ufi_readcapacity(): no response");
		return -1;
	}
	if (ufi_status() != 0) {
		bitch(BITCHWARNING,"ufi_readcapacity(): status = %u",ufi_status());
		return -1;
	}

	if (ufi_bulk_in_data_buffer_sz >= 8) {
		*llba =  ((unsigned int)inq[3])        |
			(((unsigned int)inq[2]) <<  8) |
			(((unsigned int)inq[1]) << 16) |
			(((unsigned int)inq[0]) << 24);

		*blen =  ((unsigned int)inq[7])        |
			(((unsigned int)inq[6]) <<  8) |
			(((unsigned int)inq[5]) << 16) |
			(((unsigned int)inq[4]) << 24);

		bitch(BITCHINFO,"ufi_readcapacity(): Got LLBA = %u BLEN = %u",*llba,*blen);
	}

	if (ufi_sense() < 0) {
		bitch(BITCHWARNING,"ufi_readcapacity(): sense data not returned");
		return -1;
	}
	if (ufi_sense_key() != 0) {
		bitch(BITCHWARNING,"ufi_readcapacity(): error sense key 0x%02X",ufi_sense_key());
		return -1;
	}

	return 0;
}

int openblkll(struct usb_device *dev)
{
	struct usb_config_descriptor* c;
	struct usb_interface* i;
	struct usb_interface_descriptor* id;
	struct usb_endpoint_descriptor *e;
	int ic,ii,iid,ie;

	if (is_open) {
		bitch(BITCHWARNING,"openblk() called when device open");
		closeblk();
	}

	for (ic=0;ic < dev->descriptor.bNumConfigurations && !dev_cfg;ic++) {
		c = &dev->config[ic];
		for (ii=0;ii < c->bNumInterfaces && !dev_if;ii++) {
			i = &c->interface[ii];
			for (iid=0;iid < i->num_altsetting && !dev_ifd;iid++) {
				id = &i->altsetting[iid];

				/* we are looking for: */
				if (	id->bInterfaceClass    == 8 &&	/* USB mass storage */
					id->bInterfaceSubClass == 4 &&	/* Floppy UFI */
					id->bInterfaceProtocol == 0) {	/* Control/Bulk/Interrupt */
					dev_cfg = c;
					dev_if  = i;
					dev_ifd = id;
				}
			}
		}
	}

	bitch(BITCHINFO,"Block IO driver: Linux libusb UFI driver");
	if (!dev_cfg || !dev_if || !dev_ifd) {
		bitch(BITCHWARNING,"...This USB device is not the mass storage UFI type");
		return -1;
	}

	/* find all the endpoints we need */
	dev_ep_data_in = dev_ep_data_out = dev_ep_int_in = -1;
	for (ie=0;ie < id->bNumEndpoints;ie++) {
		e = &id->endpoint[ie];

		if (e->bmAttributes == 2) { /* bulk endpoint? */
			if ((e->bEndpointAddress & 0xF0) == 0x80) { /* bulk in? */
				if (dev_ep_data_in < 0) dev_ep_data_in = e->bEndpointAddress;
			}
			else if ((e->bEndpointAddress & 0xF0) == 0x00) { /* bulk out? */
				if (dev_ep_data_out < 0) dev_ep_data_out = e->bEndpointAddress;
			}
		}
		else if (e->bmAttributes == 3) { /* interrupt endpoint? */
			if ((e->bEndpointAddress & 0xF0) == 0x80) { /* interrupt in? */
				if (dev_ep_int_in < 0) dev_ep_int_in = e->bEndpointAddress;
			}
		}
	}

	if (dev_ep_data_in >= 0)
		bitch(BITCHINFO,"...data input endpoint (BULK TRANSFER)        0x%02X",dev_ep_data_in);
	if (dev_ep_data_out >= 0)
		bitch(BITCHINFO,"...data output endpoint (BULK TRANSFER)       0x%02X",dev_ep_data_out);
	if (dev_ep_int_in >= 0)
		bitch(BITCHINFO,"...status input endpoint (INTERRUPT)          0x%02X",dev_ep_int_in);

	if (dev_ep_data_in < 0 || dev_ep_data_out < 0 || dev_ep_int_in < 0) {
		bitch(BITCHERROR,"...Could not locate all endpoints required for UFI CMD/BULK/INT protocol");
		return -1;
	}

	/* open the device */
	dev_handle = usb_open(dev);
	if (!dev_handle) {
		bitch(BITCHWARNING,"...usb_open() failed");
		return -1;
	}

	if (usb_set_configuration(dev_handle,dev_cfg->bConfigurationValue) < 0) {
		bitch(BITCHWARNING,"...cannot set configuration value");
		usb_close(dev_handle);
		return -1;
	}

	if (usb_claim_interface(dev_handle,dev_ifd->bInterfaceNumber) < 0) {
		bitch(BITCHWARNING,"...cannot claim interface");
		usb_close(dev_handle);
		return -1;
	}

	if (usb_set_altinterface(dev_handle,dev_ifd->bInterfaceNumber) < 0) {
		bitch(BITCHWARNING,"...cannot set altinterface");
		usb_close(dev_handle);
		return -1;
	}

	bitch(BITCHINFO,"Sending UFI RESET/DIAGNOSTIC");
	if (ufi_sendcmd(ufi_send_diagnostic,sizeof(ufi_send_diagnostic)) < 0)
		bitch(BITCHWARNING,"...failed");
	else if (ufi_response(0) < 0)
		bitch(BITCHWARNING,"...no response");
	else if (ufi_status() != 0)
		bitch(BITCHWARNING,"...device indicates failure for self-test");
	else
		bitch(BITCHINFO,"...self-test success");

	bitch(BITCHINFO,"Sending UFI INQUIRY");
	if (ufi_sendcmd(ufi_inquiry,sizeof(ufi_inquiry)) < 0)
		bitch(BITCHWARNING,"...INQUIRY failed");
	else if (ufi_response(1) < 0)
		bitch(BITCHWARNING,"...No response to INQUIRY");
	else {
		bitch_INQ_response();
		if (ufi_status() != 0) {
			bitch(BITCHINFO,"...UFI INQUIRY failure");
			usb_close(dev_handle);
			return -1;
		}
	}

	bitch(BITCHINFO,"Sending START UNIT command");
	if (ufi_sendcmd(ufi_start_motor,sizeof(ufi_start_motor)) < 0)
		bitch(BITCHWARNING,"...failed");
	else if (ufi_response(0) < 0)
		bitch(BITCHWARNING,"...no response");
	else if (ufi_status() != 0)
		bitch(BITCHWARNING,"...bad status");
	else
		bitch(BITCHINFO,"...ok");

	if (ufi_sense() < 0)
		bitch(BITCHWARNING,"START UNIT sense data not returned");
	else if (ufi_sense_key() >= 2)
		bitch(BITCHWARNING,"START UNIT error sense 0x%02X",ufi_sense_key());

	if (ufi_testready() < 0)
		bitch(BITCHWARNING,"TEST UNIT READY failed -- unit not ready");
	else if (ufi_sense() < 0)
		bitch(BITCHWARNING,"TEST UNIT READY sense data not returned");
	else if (ufi_sense_key() >= 2)
		bitch(BITCHWARNING,"TEST UNIT READY error sense 0x%02X",ufi_sense_key());

	if (ufi_msfdp() < 0)
		bitch(BITCHWARNING,"Unable to read Flexible Disk Page");

	return 0;
}

void closeblkll()
{
	if (dev_handle) {
		usb_close(dev_handle);
		dev_handle = NULL;
	}

	is_open = 0;
}

