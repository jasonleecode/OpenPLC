/* Minimal time.h for ARM Cortex-M0+ cross-compilation */

#ifndef __TIME_H
#define __TIME_H

#include <stdint.h>

typedef uint32_t time_t;
typedef uint32_t clock_t;

#define CLOCKS_PER_SEC 1000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

extern time_t time(time_t *tloc);
extern clock_t clock(void);
extern char *ctime(const time_t *timep);
extern struct tm *localtime(const time_t *timep);
extern struct tm *gmtime(const time_t *timep);

#endif /* __TIME_H */
