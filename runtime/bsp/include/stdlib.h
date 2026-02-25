/* Minimal stdlib.h for ARM Cortex-M0+ cross-compilation */

#ifndef __STDLIB_H
#define __STDLIB_H

#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

extern int atoi(const char *nptr);
extern long atol(const char *nptr);
extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void abort(void);
extern void exit(int status);

#endif /* __STDLIB_H */
