#include "vpn_packet_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint8_t h[38]={0}; uint32_t ap=0x0104a8c0;
    assert(vpn_packet_classify(h,13,ap)==VPN_PACKET_DROP);
    h[12]=0x86;h[13]=0xdd;assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_DROP);
    h[12]=8;h[13]=6;assert(vpn_packet_classify(h,14,ap)==VPN_PACKET_LOCAL);
    h[13]=0;h[14]=0x45;h[30]=8;h[31]=8;h[32]=8;h[33]=8;
    assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_ROUTE);
    memcpy(h+30,&ap,4);assert(vpn_packet_classify(h,34,ap)==VPN_PACKET_LOCAL);
    memset(h+30,255,4);h[23]=17;h[35]=68;h[37]=67;
    assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_LOCAL);
    h[21]=1;assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_ROUTE);h[21]=0;
    h[20]=0x20;assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_ROUTE);h[20]=0;
    h[35]=123;assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_ROUTE);
    h[14]=0x44;assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_DROP);
    h[14]=0x65;assert(vpn_packet_classify(h,38,ap)==VPN_PACKET_DROP);
    puts("Packet policy tests passed: IPv6/malformed drop, ARP/AP/DHCP access, internet requires VPN route, fragmented DHCP cannot bypass policy.");return 0;
}
