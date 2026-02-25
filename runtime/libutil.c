/* Minimal C library implementations for ARM Cortex-M0+ bare metal */

#include <stdint.h>

/* memcpy implementation */
void *memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

/* memset implementation */
void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)s;
    
    while (n--) {
        *p++ = (unsigned char)c;
    }
    
    return s;
}

/* memcmp implementation */
int memcmp(const void *s1, const void *s2, unsigned long n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

/* strlen implementation */
unsigned long strlen(const char *s) {
    unsigned long len = 0;
    
    while (*s++) {
        len++;
    }
    
    return len;
}

/* strcpy implementation */
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    
    while ((*d++ = *src++)) {
        ;
    }
    
    return dest;
}

/* strncpy implementation */
char *strncpy(char *dest, const char *src, unsigned long n) {
    char *d = dest;
    
    while (n-- && (*d++ = *src++)) {
        ;
    }
    
    while (n--) {
        *d++ = '\0';
    }
    
    return dest;
}

/* strcmp implementation */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    
    return (unsigned char)*s1 - (unsigned char)*s2;
}

/* strncmp implementation */
int strncmp(const char *s1, const char *s2, unsigned long n) {
    while (n-- && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    
    if (n == 0) {
        return 0;
    }
    
    return (unsigned char)*s1 - (unsigned char)*s2;
}
