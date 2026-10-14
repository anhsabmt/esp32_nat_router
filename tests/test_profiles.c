#include "vpn_profiles.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int mock_nvs_failure;
void mock_nvs_corrupt(const char *);
/* Synthetic test key: 32 bytes of 0x01, never a real credential. */
#define TEST_KEY "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="
static const char *valid=
    "[Interface]\nPrivateKey = " TEST_KEY "\nAddress = 10.2.0.2/32\nDNS = 10.2.0.1\n"
    "[Peer]\nPublicKey = " TEST_KEY "\nEndpoint = vpn.example.com:51820\nAllowedIPs = 0.0.0.0/0, ::/0\nPersistentKeepalive = 25\n";
static int checks;
static void invalid(const char *text) {
    vpn_profile_t p; char error[160]; memset(&p,0xff,sizeof(p));
    assert(vpn_profile_parse(text,"test",&p,error,sizeof(error))!=ESP_OK);
    assert(!strstr(error,TEST_KEY));
    unsigned char *bytes=(void *)&p;
    for(size_t i=0;i<sizeof(p);i++) assert(bytes[i]==0);
    checks++;
}
static void replace_invalid(const char *old,const char *replacement) {
    char buf[4097]; const char *start=strstr(valid,old); assert(start);
    size_t before=start-valid;
    memcpy(buf,valid,before); buf[before]=0; strcat(buf,replacement); strcat(buf,start+strlen(old)); invalid(buf);
}
int main(void) {
    vpn_profile_t p,q; char error[160];
    assert(vpn_profile_parse(valid,"US New York",&p,error,sizeof(error))==ESP_OK);
    assert(!strcmp(p.netmask,"255.255.255.255") && p.keepalive==25 && p.mtu==1420); checks++;
    replace_invalid("/32","/33");
    replace_invalid("10.2.0.2","999.2.0.2");
    replace_invalid("DNS = 10.2.0.1\n","");
    replace_invalid("DNS = 10.2.0.1","DNS = bad.example.com");
    replace_invalid("51820","65536");
    replace_invalid("51820","-1");
    replace_invalid("25","25oops");
    replace_invalid("0.0.0.0/0, ::/0","10.0.0.0/8");
    replace_invalid("0.0.0.0/0, ::/0","0.0.0.0/0, 10.0.0.0/8");
    replace_invalid("vpn.example.com","[::1]");
    replace_invalid("[Peer]","[Peer]\nPostUp = echo bad");
    replace_invalid("[Peer]","[Peer]\n[Peer]");
    replace_invalid("PrivateKey = " TEST_KEY,"PrivateKey = invalid");
    replace_invalid("[Peer]","[Peer]\nPublicKey = " TEST_KEY);
    char text[4097]; snprintf(text,sizeof(text),"%sMTU = 1421\n",valid); invalid(text);
    snprintf(text,sizeof(text),"[Interface]\nPrivateKey = " TEST_KEY "\nAddress = fd00::2/128, 10.4.5.2/24\nDNS = fd00::1, 10.4.5.1\nMTU = 1280\n[Peer]\nPublicKey = " TEST_KEY "\nEndpoint = 1.2.3.4:53\nAllowedIPs = ::/0, 0.0.0.0/0\n");
    assert(vpn_profile_parse(text,"Dual stack",&q,error,sizeof(error))==ESP_OK);
    assert(!strcmp(q.netmask,"255.255.255.0") && !strcmp(q.dns,"10.4.5.1")); checks++;
    assert(vpn_profile_parse(valid,"",&q,error,sizeof(error))!=ESP_OK); checks++;
    assert(vpn_profiles_init()==ESP_OK);
    assert(vpn_profile_load(0,&q)==ESP_ERR_NVS_NOT_FOUND); checks++;
    for(int i=0;i<VPN_PROFILE_MAX;i++) { assert(vpn_profile_save(i,&p)==ESP_OK); assert(vpn_profile_load(i,&q)==ESP_OK); assert(!memcmp(&p,&q,sizeof(p))); checks++; }
    assert(vpn_profile_save(8,&p)==ESP_ERR_INVALID_ARG); checks++;
    assert(vpn_selection_save(3,true)==ESP_OK);
    int id=-1; bool connected=false; assert(vpn_selection_load(&id,&connected)==ESP_OK && id==3 && connected); checks++;
    assert(vpn_selection_save(3,false)==ESP_OK); assert(vpn_selection_load(&id,&connected)==ESP_OK && id==3 && !connected); checks++;
    mock_nvs_failure=1; assert(vpn_selection_save(2,true)!=ESP_OK); mock_nvs_failure=0;
    assert(vpn_selection_load(&id,&connected)==ESP_OK && id==3 && !connected); checks++;
    mock_nvs_corrupt("p0"); assert(vpn_profile_load(0,&q)==ESP_ERR_INVALID_STATE); checks++;
    mock_nvs_corrupt("selection"); assert(vpn_selection_load(&id,&connected)==ESP_ERR_INVALID_STATE); checks++;
    assert(vpn_profile_delete(1)==ESP_OK && vpn_profile_load(1,&q)==ESP_ERR_NVS_NOT_FOUND); checks++;
    puts("Profile parser/NVS tests passed"); printf("%d checks; profile blob %u bytes\n",checks,(unsigned)sizeof(p));
    return 0;
}
