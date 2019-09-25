/* high-level blockio code for jarchdvd.
 * uses the low-level code to send SCSI commands */

#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
#include <sys/ioctl.h>
#include <asm/fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <linux/cdrom.h>
#include <linux/fs.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "blockio.h"
#include "bitchin.h"
#include "config.h"
#include <usb.h>

extern int			fast_ripping;

static int			blk_open = 0;
static int			alloc_sz = 0;
static unsigned char*		alloc_buf = NULL;
static unsigned long		next_sector = 0;

static int			init_usb = 0;
static int			usb_bus_changes = 0,usb_dev_changes = 0;
static struct usb_bus*		usb_busses = NULL;
static struct usb_device*	usb_dev = NULL;

int ScanUSB()
{
	if (!init_usb) {
		usb_init();
		init_usb=1;
	}

	usb_bus_changes = usb_find_busses();
	usb_dev_changes = usb_find_devices();
	usb_busses = usb_get_busses();
	return 0;
}

int GetCapacity()
{
	return 0;
}

/* where name = string of the form "bus:device" */
int FindDev(char *name,struct usb_device **e)
{
	struct usb_device *d;
	struct usb_bus *b = usb_busses;
	char *ss;
	int B,D,i;

	ss = strchr(name,':');
	if (!ss) return -1;
	if (!isdigit(*name)) return -1;
	B = atoi(name);
	name = ss+1;
	if (!isdigit(*name)) return -1;
	D = atoi(name);
	if (!b) return -1;
	if (!e) return -1;

	for (i=1;i != B && b;b=b->next) i++;
	if (!b) {
		bitch(BITCHERROR,"No such USB bus %u",B);
		return -1;
	}

	d=b->devices;
	for (;d && d->devnum != D;d=d->next);
	if (!d) {
		bitch(BITCHERROR,"No such USB device %u on USB bus %u",D,B);
		return -1;
	}

	*e = d;
	return 0;
}

int openblk(char *name)
{
	if (blk_open)
		return 0;

	if (ScanUSB() < 0)
		return 0;

	if (FindDev(name,&usb_dev) < 0)
		return 0;

	if (openblkll(usb_dev) < 0)
		return 0;

	alloc_sz = 18 * 512;
	bitch(BITCHINFO,"Allocating memory to hold %d sectors",alloc_sz);

	alloc_buf = (unsigned char*)malloc(alloc_sz);
	if (!alloc_buf) {
		bitch(BITCHINFO,"Unable to allocate sector buffer!");
		closeblk();
		return 0;
	}

	blk_open = 1;
	next_sector = 0;
	return 1;
}

void closeblk()
{
	if (alloc_buf) free(alloc_buf);
	alloc_buf = NULL;
	closeblkll();
	blk_open = 0;
}

unsigned char *getbufblk()
{
	if (blk_open == 0) return NULL;
	return alloc_buf;
}

int getbufblksize()
{
	if (blk_open == 0) return 0;
	return alloc_sz;
}

int readblk(int sectors,int sz)
{
	if (blk_open == 0) return 0;
	if (sz > alloc_sz) return 0;
	return 0;
}

int seekblk(unsigned long sector)
{
	next_sector = sector;
	return 1;
}

