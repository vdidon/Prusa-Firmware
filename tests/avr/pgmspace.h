/**
 * @file
 * @brief Host replacement of avr-libc <avr/pgmspace.h>: the flash is plain memory on the host.
 */

#ifndef TESTS_AVR_PGMSPACE_H_
#define TESTS_AVR_PGMSPACE_H_

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))

static inline int strncasecmp_P(const char *s1, const char *s2, size_t n) {
    for (; n; --n, ++s1, ++s2) {
        int d = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (d || !*s1)
            return d;
    }
    return 0;
}

#endif /* TESTS_AVR_PGMSPACE_H_ */
