#pragma once
#include "ip_addr.h"
typedef unsigned short u16_t;
struct netif { ip4_addr_t ip_addr; unsigned mtu; };
extern struct netif *netif_default;
