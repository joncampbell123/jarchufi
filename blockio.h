
#ifndef __INCLUDE_BLOCKIO_H
#define __INCLUDE_BLOCKIO_H

#include "config.h"
#include <usb.h>

/* high-level blockio */
int openblk(char *name);
unsigned char *getbufblk();
int getbufblksize();
int readblk(int sectors,int sz);
int readblktime();
int seekblk(unsigned long sector);
void closeblk();
int GetCapacity();

/* low-level blockio */
int openblkll(struct usb_device *dev);
void closeblkll();
int ufi_read(unsigned int LBA,unsigned int blocks,unsigned char *buf,int bufsz);
int ufi_read_capacity(unsigned int *LLBA,unsigned int *blen);

#endif //__INCLUDE_BLOCKIO_H

