
#include <sys/stat.h>
#include <sys/types.h>
#ifdef LINUX
#include <sys/ioctl.h>
#include <asm/fcntl.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include "blockio.h"
#include "bitchin.h"
#include "main.h"

static char *CLASSES[] = {
	"",			// DUMMY
	":<ERROR>:  ",
	":<WARNING>:",
	":<INFO>:   ",
	NULL
};

static FILE* bitchfp = NULL;

void bitch(int CLASS,char *fmt,...)
{
	va_list va;

	if (CLASS < 0 || CLASS > BITCHINFO)
		CLASS = BITCHERROR;

	va_start(va,fmt);
	
	if (bitchfp) {
		fprintf(bitchfp,"%s",CLASSES[CLASS]);
		vfprintf(bitchfp,fmt,va);
		fprintf(bitchfp,"\n");
		fflush(bitchfp);
	}
	
	/* and to STDOUT */
	printf("%s",CLASSES[CLASS]);
	vprintf(fmt,va);
	printf("\n");
	fflush(stdout);
	
	va_end(va);
}

static char nothing[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};

void bitch_init(FILE *output)
{
	if (output == stdout || output == stderr)
		bitchfp = NULL;
	else
		bitchfp = output;
}

