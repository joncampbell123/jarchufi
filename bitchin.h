
#ifndef __INCLUDE_BITCHIN_H
#define __INCLUDE_BITCHIN_H

enum {
	BITCHERROR=1,
	BITCHWARNING,
	BITCHINFO,
};

void bitch(int CLASS,char *fmt,...);
void bitch_init(FILE *outfp);

#endif //__INCLUDE_BITCHIN_H

