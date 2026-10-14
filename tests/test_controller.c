/* Compile the real controller with network/RTOS adapters; never claims hardware validation. */
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vpn_config.h"
int32_t vpn_enabled=0,vpn_port=51820,vpn_keepalive=0,vpn_mtu=1420;
int32_t vpn_killswitch=1,vpn_route_all=1;
char *vpn_private_key,*vpn_public_key,*vpn_preshared_key,*vpn_endpoint,*vpn_address,*vpn_netmask,*vpn_dns;
bool vpn_connected=false,ap_connect=true;
uint32_t vpn_tunnel_ip,my_ap_ip=0x0104a8c0;
uint8_t ap_nat_enabled=1;
uint16_t ap_mss_clamp,ap_pmtu;
#include "../main/vpn_manager.c"
static int lock_depth,live_tunnels,max_tunnels,connects,disconnects,fail_connect,switch_during_dns;
static int notifications,step_count;
static jmp_buf step_end;
static struct netif uplink,tunnel;
struct netif *netif_default=&uplink;
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
void xSemaphoreTake(SemaphoreHandle_t s,unsigned ticks){assert(s && !lock_depth);lock_depth++;}
void xSemaphoreGive(SemaphoreHandle_t s){assert(lock_depth==1);lock_depth--;}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned size,void *arg,unsigned prio,TaskHandle_t *t){*t=(void *)2;return pdPASS;}
void xTaskNotifyGive(TaskHandle_t t){assert(t);notifications++;}
unsigned ulTaskNotifyTake(int clear,unsigned ticks){if(step_count++)longjmp(step_end,1);return 1;}
int tcpip_callback_wait(void (*fn)(void *),void *arg){fn(arg);return ERR_OK;}
int ip4addr_aton(const char *text,ip4_addr_t *a){a->addr=0x0200020a;return 1;}
struct netif *ip4_route(const ip4_addr_t *dst){return netif_default;}
void ip_napt_enable(uint32_t ip,int enabled){}
void delete_portmap_tab(void){}
void apply_portmap_tab(void){}
void vpn_refresh_ap_dns(void){}
int64_t esp_timer_get_time(void){return 100000000;}
int esp_sntp_enabled(void){return 1;}
void esp_sntp_setoperatingmode(int x){}
void esp_sntp_setservername(int i,const char *s){}
void esp_sntp_init(void){}
int getaddrinfo(const char *host,const char *service,const struct addrinfo *hints,struct addrinfo **out){
    static struct sockaddr_in address;static struct addrinfo result;
    assert(!lock_depth);
    if(switch_during_dns){switch_during_dns=0;assert(vpn_profile_select(1,false)==ESP_OK);}
    result.ai_addr=&address;*out=&result;return 0;
}
void freeaddrinfo(struct addrinfo *r){}
const char *inet_ntop(int family,const void *addr,char *out,size_t n){strcpy(out,"1.2.3.4");return out;}
esp_err_t esp_wireguard_init(wireguard_config_t *config,wireguard_ctx_t *ctx){ctx->config=config;ctx->netif_default=netif_default;return ESP_OK;}
esp_err_t esp_wireguard_connect(wireguard_ctx_t *ctx){
    assert(live_tunnels==0);ctx->netif=&tunnel;live_tunnels++;connects++;
    if(live_tunnels>max_tunnels)max_tunnels=live_tunnels;
    return fail_connect?ESP_FAIL:ESP_OK;
}
esp_err_t esp_wireguard_disconnect(wireguard_ctx_t *ctx){
    if(ctx->netif){assert(live_tunnels==1);live_tunnels--;disconnects++;netif_default=ctx->netif_default;ctx->netif=NULL;}return ESP_OK;
}
esp_err_t esp_wireguard_set_default(wireguard_ctx_t *ctx){netif_default=ctx->netif;return ESP_OK;}
esp_err_t esp_wireguardif_peer_is_up(wireguard_ctx_t *ctx){return ctx->netif?ESP_OK:ESP_FAIL;}
static void step(void){step_count=0;if(!setjmp(step_end))vpn_connect_task(NULL);assert(!lock_depth);}
#define TEST_KEY "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="
int main(void){
    vpn_private_key=calloc(1,1);vpn_public_key=calloc(1,1);vpn_preshared_key=calloc(1,1);
    vpn_endpoint=calloc(1,1);vpn_address=calloc(1,1);vpn_netmask=calloc(1,1);vpn_dns=calloc(1,1);
    assert(vpn_profiles_boot()==ESP_OK);
    const char *conf="[Interface]\nPrivateKey = " TEST_KEY "\nAddress = 10.2.0.2/32\nDNS = 10.2.0.1\n[Peer]\nPublicKey = " TEST_KEY "\nEndpoint = vpn.example.com:51820\nAllowedIPs = 0.0.0.0/0\n";
    char msg[160];int id;
    assert(vpn_profile_import(-1,"A",conf,&id,msg,sizeof(msg))==ESP_OK && id==0);
    assert(vpn_profile_import(-1,"B",conf,&id,msg,sizeof(msg))==ESP_OK && id==1);
    assert(vpn_profile_select(0,true)==ESP_OK);step();step();
    assert(live_tunnels==1 && vpn_guard_ready() && selected==0);
    vpn_profile_t metadata;assert(vpn_profile_get(0,&metadata)==ESP_OK);
    assert(!metadata.private_key[0] && !metadata.preshared_key[0]);
    assert(vpn_profile_remove(0)==ESP_ERR_INVALID_STATE);
    assert(vpn_profile_update(0,&metadata)==ESP_ERR_INVALID_STATE);
    assert(vpn_profile_select(1,true)==ESP_OK && live_tunnels==0 && vpn_profile_guard);
    step();step();assert(live_tunnels==1 && max_tunnels==1 && connects==2 && disconnects==1);
    netif_default=&uplink;assert(!vpn_guard_ready());vpn_reassert_default_route();assert(vpn_guard_ready());
    assert(vpn_profile_stop()==ESP_OK && live_tunnels==0 && vpn_profile_guard);step();assert(live_tunnels==0);
    int saved;bool connect;assert(vpn_selection_load(&saved,&connect)==ESP_OK && saved==1 && !connect);
    /* Switching during endpoint resolution must cancel the old attempt. */
    assert(vpn_profile_select(0,true)==ESP_OK);switch_during_dns=1;step();
    assert(selected==1 && !desired && live_tunnels==0 && connects==2);step();assert(live_tunnels==0);
    fail_connect=1;assert(vpn_profile_select(0,true)==ESP_OK);step();
    assert(live_tunnels==0 && vpn_profile_guard && last_error==ESP_FAIL);
    fail_connect=0;assert(vpn_profile_select(0,true)==ESP_OK);step();step();
    assert(live_tunnels==1 && max_tunnels==1);
    ap_connect=false;vpn_disconnect();assert(live_tunnels==0 && vpn_profile_guard);step();
    ap_connect=true;step();step();assert(live_tunnels==1);
    assert(vpn_router_mode()==ESP_OK && !vpn_profile_guard && live_tunnels==0 && selected==-1);step();assert(live_tunnels==0);
    assert(vpn_profile_remove(1)==ESP_OK);
    extern int mock_nvs_failure;
    assert(vpn_profile_select(0,true)==ESP_OK);step();step();
    mock_nvs_failure=1;assert(vpn_router_mode()!=ESP_OK);
    assert(vpn_profile_guard && live_tunnels==0);step();assert(live_tunnels==0);
    mock_nvs_failure=0;assert(vpn_router_mode()==ESP_OK);
    for(int i=1;i<VPN_PROFILE_MAX;i++) {
        assert(vpn_profile_import(-1,"Extra",conf,&id,msg,sizeof(msg))==ESP_OK && id==i);
    }
    assert(vpn_profile_import(-1,"Ninth",conf,&id,msg,sizeof(msg))==ESP_ERR_NO_MEM);
    puts("Controller integration tests passed: switch, disconnect, DHCP route recovery, cancel pending DNS, failure cleanup, reconnect, redaction, router mode; max one tunnel.");
    return 0;
}
