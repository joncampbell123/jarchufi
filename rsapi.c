
#ifdef LINUX
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <asm/fcntl.h>
#include <unistd.h>
#endif
#ifdef WIN32
#include <string.h>
#include <fcntl.h>
#include <io.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "blockio.h"
#include "bitchin.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* simple API to maintain a bitmap of which sector has been read
 * and which sector hasn't been read so we can recover the ones
 * that haven't been read without scanning all the data (makes
 * recovery over NFS or Samba shares faster!) */
int rsapi_fd = -1;

/* given file "name" reset (delete) the corresponding bitmap file */
int rsapi_clear(char *name)
{
	char tmp[256];

	sprintf(tmp,"%s.recovery-bmp",name);
	remove(tmp);
	return 1;
}

/* given file "name" opens/creates a bitmap file named name.recovery-bmp */
int rsapi_open(char *name)
{
	char tmp[256];

	sprintf(tmp,"%s.recovery-bmp",name);
	
	if (rsapi_fd >= 0) {
		bitch(BITCHERROR,"BUG BUG BUG rsapi_open() called when bitmap file already open!");
		close(rsapi_fd);
		rsapi_fd = -1;
	}

	rsapi_fd = open(tmp,O_RDWR | O_CREAT | O_BINARY,0644);
	if (rsapi_fd < 0) {
		bitch(BITCHERROR,"Cannot open bitmap file %s for %s",tmp,name);
		return 0;
	}

	fchmod(rsapi_fd,0644);
	return 1;
}

/* get bitmap size */
unsigned long rsapi_size()
{
	unsigned long x;

	if (rsapi_fd < 0) return 0;
	x = lseek(rsapi_fd,0,SEEK_END);
	if (x < 0) return 0;
	x <<= 3L;
	return x;
}

int rsapi_close()
{
	if (rsapi_fd >= 0) {
		close(rsapi_fd);
		rsapi_fd = -1;
	}

	return 1;
}

int rsapi_read(unsigned long idx)
{
	unsigned long x,o;
	unsigned char c,s;

	if (rsapi_fd < 0) return 0;

	s = idx&7;
	o = idx>>3;
	x = lseek(rsapi_fd,o,SEEK_SET);
	if (x < 0) {
		bitch(BITCHWARNING,"rsapi_read() cannot lseek() to index %lu offset %lu",idx,o);
		return 0;
	}

	/* if we read beyond the EOF then "read" a 0 */
	if (x < o) return 0;
	if (read(rsapi_fd,&c,1) < 1) return 0;

	/* return bit value */
	return ((int) ((c >> s) & 1) );
}

int rsapi_write(unsigned long idx,char bit)
{
	unsigned long x,o;
	unsigned char c,s;

	if (rsapi_fd < 0) return 0;

	s = idx&7;
	o = idx>>3;
	x = lseek(rsapi_fd,o,SEEK_SET);
	if (x < 0) {
		bitch(BITCHWARNING,"rsapi_write() cannot lseek() to index %lu offse %lu",idx,o);
		return 0;
	}

	/* pad file out to necessary length if necessary to fulfill request */
	while (x < o) {
		c = 0;
		write(rsapi_fd,&c,1);
		x++;
	}

	if (read(rsapi_fd,&c,1) < 1) c=0;
	c |= (bit & 1) << s;
	if (lseek(rsapi_fd,o,SEEK_SET) != o) {
		bitch(BITCHWARNING,"rsapi_write() cannot lseek() to index %lu offset %lu for write()",idx,o);
		return 0;
	}
	if (write(rsapi_fd,&c,1) < 1) {
		bitch(BITCHWARNING,"rsapi_write() index %lu offset %lu write() failed",idx,o);
	}

	return 1;
}

