/* Minimal stdio.h for ARM Cortex-M0+ cross-compilation */

#ifndef __STDIO_H
#define __STDIO_H

#include <stdint.h>

#define EOF (-1)

extern int printf(const char *format, ...);
extern int sprintf(char *str, const char *format, ...);
extern int snprintf(char *str, unsigned long size, const char *format, ...);

#endif /* __STDIO_H */
