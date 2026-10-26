#ifndef BITMASK_H
#define BITMASK_H

#include <stdint.h>

typedef unsigned char uc_t;
typedef unsigned short us_t;

void val_bmk_array(void);

uint32_t is_lower(const uc_t c);
uint32_t is_upper(const uc_t c);
uint32_t is_digit(const uc_t x);
uint32_t is_alnum(const uc_t c);
uint32_t is_space(const uc_t x);

#endif
