/* Ethernet preclassification; route/peer checks happen on the TCP/IP thread.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef enum { VPN_PACKET_DROP, VPN_PACKET_LOCAL, VPN_PACKET_ROUTE } vpn_packet_class_t;
static inline vpn_packet_class_t vpn_packet_classify(const uint8_t *header,size_t count,uint32_t ap_ip) {
    if (!header || count<14) return VPN_PACKET_DROP;
    if (header[12]==0x08 && header[13]==0x06) return VPN_PACKET_LOCAL; /* ARP */
    if (header[12]!=0x08 || header[13]!=0x00 || count<34 ||
        (header[14]>>4)!=4 || (header[14]&15)<5) return VPN_PACKET_DROP;
    uint32_t dest; memcpy(&dest,header+30,sizeof(dest));
    if (dest==ap_ip) return VPN_PACKET_LOCAL;
    /* Only unfragmented DHCP broadcasts bypass an unavailable tunnel. */
    if (count>=38 && header[14]==0x45 && header[23]==17 &&
        (header[20]&0x3f)==0 && header[21]==0 &&
        header[34]==0 && header[35]==68 && header[36]==0 && header[37]==67 &&
        dest==0xffffffffU) return VPN_PACKET_LOCAL;
    return VPN_PACKET_ROUTE;
}
