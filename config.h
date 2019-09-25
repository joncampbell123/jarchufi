
#ifndef CONFIG_H
#define CONFIG_H

#ifdef LINUX
typedef unsigned long long			juint64;
#endif
#ifdef WIN32
typedef unsigned __int64			juint64;
#endif

#endif //CONFIG_H
