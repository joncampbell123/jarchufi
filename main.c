/* jarchufi (floppy disk archiving program, for use with USB floppy drives)
 * main.c
 *
 * (C) 2005 Jonathan Campbell
 */

#ifdef LINUX
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <asm/fcntl.h>
#include <unistd.h>
#include <signal.h>
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
#include "rsapi.h"

int		STOP=0;

int		bad_sector_count=0;
int		skipped_sector_count=0;

static FILE*	chosen_bitch_out;
static char	chosen_bitch_out_fname[256];
static char	chosen_input_block_dev[256];

void sigma(int x)
{
	if (x == SIGQUIT || x == SIGINT || x == SIGTERM)
		STOP=1;
}

int CopyThatFloppy()
{
	unsigned long sofs;
	unsigned char *buf;
	char imgname[64];
	char imgrn[64];
	int buf_max;
	int capmax;
	int ssize;
	int fd,s;
	int rd,i;
	int rdmax;
	int rdnext;
	int rdb;

	buf = getbufblk();
	buf_max = getbufblksize();

	/* what does a USB floppy drive do when it encounters floppies with multiple sector sizes? */
	/* just curious.... */
	if (ufi_read_capacity(&capmax,&ssize) < 0) {
		bitch(BITCHWARNING,"Unable to obtain UFI device capacity");
		return -1;
	}
	else if (ssize < 1) {
		bitch(BITCHWARNING,"UFI device returned bad sector size");
		return -1;
	}
	capmax++;
	bitch(BITCHINFO,"UFI device reports %u sectors, %u bytes/sector",capmax,ssize);

	sprintf(imgname,"fdd.%u-img",ssize);
	sprintf(imgrn,"fdd.%u-img.recovery-bitmap",ssize);

	fd = open(imgname,O_RDWR|O_CREAT,0644);
	if (fd < 0) {
		bitch(BITCHERROR,"Unable to open/create %s",imgname);
		return -1;
	}

	if (rsapi_open(imgrn) <= 0) {
		bitch(BITCHERROR,"Unable to open recovery bitmap for %s",imgrn);
		close(fd);
		return -1;
	}

	rdnext = rdmax = buf_max / ssize;
	for (s=0;s < capmax && !STOP;) {
		/* skip past already ripped sectors? */
		while (s < capmax && rsapi_read(s)) s++;

		/* how many consecutive sectors? */
		for (i=0;(s+i) < capmax && !rsapi_read(s+i) && i < rdnext;) i++;

		/* exit out if done */
		if (s >= capmax || i == 0) break;

		/* now rip */
		printf("reading sector %u (x %u)   ",s,i); fflush(stdout);
		rdb = ufi_read(s,i,buf,buf_max);
		printf("\x0D                                    \x0D");
		if (rdb < 0) {
			bitch(BITCHWARNING,"Unable to rip sector %u",s);
			s++;
		}
		else {
			if (rdb % ssize) {
				bitch(BITCHWARNING,"Got data back len %u which is not a multiple of %u, rounding down",
					rdb,ssize);

				rdb -= rdb % ssize;
			}

			rd = rdb / ssize;
			if (!rd) {
				bitch(BITCHWARNING,"Didn't get any data for sector %u",s);
				rdnext=1;
				s++;
			}
			else {
				int j;

				if (rd < i) rdnext = 1;
				else if (rdnext < rdmax) rdnext++;

				sofs = s*ssize;
				if (lseek(fd,sofs,SEEK_SET) != sofs) {
					if (ftruncate(fd,sofs) < 0) {
						bitch(BITCHWARNING,"lseek()+ftruncate() failed, unable to extend image file");
						rsapi_close();
						close(fd);
						return -1;
					}
					else if (lseek(fd,sofs,SEEK_SET) != sofs) {
						bitch(BITCHWARNING,"lseek()+ftruncate()+lseek() failed, quitting");
						rsapi_close();
						close(fd);
						return -1;
					}
				}

				if (write(fd,buf,rd*ssize) < (rd*ssize)) {
					bitch(BITCHWARNING,"write() failed, unable to extend image file");
					rsapi_close();
					close(fd);
					return -1;
				}

				for (j=0;j < rd;j++)
					rsapi_write(s+j,1);

				s += rd;
			}
		}
	}

	rsapi_close();
	close(fd);
	return 0;
}

int params(int argc,char **argv)
{
	char *p;
	int i;

	chosen_bitch_out = NULL;
	chosen_bitch_out_fname[0] = 0;
	chosen_input_block_dev[0] = 0;

	for (i=1;i < argc;i++) {
		if (argv[i][0] == '-') {
			p = argv[i]+1;

			/* -dev <dev>
			 * -dev=<dev>
			 *
			 *  either way works with me */
			if (!strncmp(p,"dev",3)) {
				p += 3;
				if (*p == '=') {
					p++;
					strcpy(chosen_input_block_dev,p);
				}
				else {
					i++;
					if (i < argc)	strcpy(chosen_input_block_dev,argv[i]);
					else		bitch(BITCHERROR,"-dev requires another argument");
				}
			}
			/* -bout <file>
			 * -bout=<file>
			 *
			 *  either way...
			 *  if not specified, all bitchin' and moanin' is routed to standard output. */
			else if (!strncmp(p,"bout",4)) {
				p += 4;
				if (*p == '=') {
					p++;
					strcpy(chosen_bitch_out_fname,p);
				}
				else {
					i++;
					if (i < argc)	strcpy(chosen_bitch_out_fname,argv[i]);
					else		bitch(BITCHERROR,"-bout requires another argument");
				}
			}
			else {
				bitch(BITCHERROR,"Unknown command line argument %s",argv[i]);
				bitch(BITCHINFO,"Valid switches are:");
				bitch(BITCHINFO,"-dev <device>               Specify which USB device to use where <device>");
				bitch(BITCHINFO,"                            is bus:device. To obtain the correct numbers use");
				bitch(BITCHINFO,"                            the lsusb command and look for the device of interest");
				bitch(BITCHINFO,"-bout <file>                Log output to <file>, append if exists");
				return 0;
			}
		}
		else {
			bitch(BITCHERROR,"Unknown command line argument %s",argv[i]);
			return 0;
		}
	}

	/* defaults? */
	if (!chosen_input_block_dev[0])
#ifdef WIN32
		Win32FindFirstDVDROMDrive(chosen_input_block_dev);
#else
		strcpy(chosen_input_block_dev,"/dev/cdrom");
#endif

	return 1;
}

int main(int argc,char **argv)
{
	char tmpest[512];
	int i;

#ifdef WIN32
	void WinSetTitle(char *x);
	WinSetTitle("Jonathan's UFI USB storage media archiving program");
#endif

	signal(SIGINT,sigma);
	signal(SIGQUIT,sigma);
	signal(SIGTERM,sigma);

	/* initial bitchin' output vector */
	bitch_init(stderr);

	/* parameters? */
	if (!params(argc,argv)) {
		bitch(BITCHERROR,"There is an error in the command line argument list");
		return 1;
	}

	/* redirect bitchin' */
	if (strlen(chosen_bitch_out_fname)) {
		chosen_bitch_out = fopen(chosen_bitch_out_fname,"rb+");
		if (!chosen_bitch_out) chosen_bitch_out = fopen(chosen_bitch_out_fname,"w");

		if (!chosen_bitch_out) {
			bitch(BITCHERROR,"Cannot open output file %s for writing",chosen_bitch_out_fname);
			return 1;
		}
		else {
			fseek(chosen_bitch_out,0,SEEK_END);
		}
	}
	else {
		/* all bitchin goes to standard output */
		chosen_bitch_out = stdout;
	}

	bitch_init(chosen_bitch_out);
	
	bitch(BITCHINFO,"------------------ <<< NEW SESSION >>> --------------------");
	bitch(BITCHINFO,"Operating system................... Linux");

	tmpest[0]=0;
	bitch(BITCHINFO,"Command line options:");
	for (i=1;i < argc;i++) {
		strcat(tmpest,argv[i]);
		strcat(tmpest," ");
	}
	bitch(BITCHINFO,"...\"%s\"",tmpest);

	bitch(BITCHINFO,"Opening block device");
	if (!openblk(chosen_input_block_dev))
		return 1;

	if (CopyThatFloppy() < 0)
		bitch(BITCHINFO,"Problem copying the floppy");

	/* close block device */
	bitch(BITCHINFO,"Closing block device");
	closeblk();

	/* NOTICE!! */
	if (bad_sector_count > 0) {
		bitch(BITCHWARNING,"*** %u bad sectors were encountered during the rip",bad_sector_count);
		bitch(BITCHWARNING,"*** Consider re-ripping to fill in the image");
	}
	if (skipped_sector_count > 0) {
		bitch(BITCHWARNING,"*** %u sectors have not been ripped",skipped_sector_count);
		bitch(BITCHWARNING,"*** Consider re-ripping to fill in the image");
	}

	/* close down bitching */
	if (chosen_bitch_out != stdout) {
		bitch_init(NULL);
		fclose(chosen_bitch_out);
	}
}

#ifdef WIN32

#include <windows.h>

void WinSetTitle(char *x)
{
	SetConsoleTitle((LPCTSTR)x);
}

void Win32FindFirstDVDROMDrive(char *x)
{
	char nam[32];
	int i;

	for (i=0;i < 26;i++) {
		nam[0] = 'A'+i;
		nam[1] = ':';
		nam[2] = '\\';
		nam[3] = 0;

		if (GetDriveType(nam) == DRIVE_CDROM) {
			sprintf(x,"\\\\.\\%c:",i+'A');
			return;
		}
	}
}

#endif
