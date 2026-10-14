#pragma once
#include <stdint.h>
typedef struct { uint32_t addr; } ip4_addr_t;
#define ip_2_ip4(a) (a)
int ip4addr_aton(const char *,ip4_addr_t *);
