/* Serialized WireGuard lifecycle and profile controller. SPDX-License-Identifier: MIT */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_wireguard.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/tcpip.h"
#include "lwip/ip4.h"
#include "lwip/lwip_napt.h"
#include "vpn_config.h"
#include "vpn_profiles.h"
#include "router_globals.h"
#include "portmap.h"

static const char *TAG="vpn_mgr";
static wireguard_config_t wg_config=ESP_WIREGUARD_CONFIG_DEFAULT();
static wireguard_ctx_t wg_ctx={0};
static bool wg_initialized;
static SemaphoreHandle_t control;
static TaskHandle_t worker;
static vpn_profile_t active;
static bool using_profile;
static int selected=-1;
static bool desired;
static uint32_t generation;
static esp_err_t last_error;
static const char *state="router";
static uint32_t vpn_subnet_ip, vpn_subnet_mask;
static char resolved_endpoint[16];
static volatile bool peer_ready;
volatile bool vpn_profile_guard;

void vpn_set_subnet(uint32_t ip,uint32_t mask) { vpn_subnet_ip=ip; vpn_subnet_mask=mask; }
IRAM_ATTR bool vpn_in_subnet(uint32_t ip) { return vpn_subnet_mask && (ip&vpn_subnet_mask)==vpn_subnet_ip; }
/* Packet hooks run on the TCP/IP thread; never wait on control there. */
IRAM_ATTR bool vpn_guard_ready(void) {
    return peer_ready && vpn_connected && wg_ctx.netif && netif_default==wg_ctx.netif;
}
bool vpn_guard_route(uint32_t destination) {
    ip4_addr_t dst={.addr=destination};
    return vpn_guard_ready() && ip4_route(&dst)==wg_ctx.netif;
}
static void read_peer(void *unused) {
    peer_ready=wg_initialized && vpn_connected && wg_ctx.netif &&
        esp_wireguardif_peer_is_up(&wg_ctx)==ESP_OK;
}
bool vpn_is_connected(void) { return peer_ready; }
int vpn_selected_profile(void) { return selected; }
const char *vpn_status(void) { return state; }
esp_err_t vpn_last_error(void) { return last_error; }
static void lock(void) { xSemaphoreTake(control,portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(control); }

static void stop_core(void *unused) {
    peer_ready=false; vpn_connected=false; vpn_tunnel_ip=0;
    ap_mss_clamp=0; ap_pmtu=0;
    if (wg_initialized) esp_wireguard_disconnect(&wg_ctx);
    wg_initialized=false; memset(&wg_ctx,0,sizeof(wg_ctx));
    delete_portmap_tab();
    /* Remove translations from the previous tunnel before accepting traffic. */
    if (my_ap_ip && ap_nat_enabled) {
        ip_napt_enable(my_ap_ip,0); ip_napt_enable(my_ap_ip,1);
    }
    apply_portmap_tab();
}
static esp_err_t stop_locked(void) {
    return tcpip_callback_wait(stop_core,NULL)==ERR_OK ? ESP_OK : ESP_FAIL;
}
void vpn_disconnect(void) {
    if (!control) return;
    lock(); generation++; last_error=stop_locked();
    state=using_profile && desired ? "waiting_uplink" : "disconnected";
    unlock();
}
static void route_core(void *unused) {
    if (vpn_enabled && vpn_route_all && wg_initialized && wg_ctx.netif && vpn_connected)
        esp_wireguard_set_default(&wg_ctx);
}
void vpn_reassert_default_route(void) { tcpip_callback_wait(route_core,NULL); }

static void connect_core(void *arg) {
    esp_err_t *result=arg;
    wg_config.private_key=vpn_private_key; wg_config.public_key=vpn_public_key;
    wg_config.preshared_key=vpn_preshared_key && vpn_preshared_key[0] ? vpn_preshared_key : NULL;
    wg_config.allowed_ip=vpn_address; wg_config.allowed_ip_mask=vpn_netmask;
    /* Resolve hostnames BEFORE entering the TCP/IP thread. */
    wg_config.endpoint=resolved_endpoint; wg_config.port=vpn_port;
    wg_config.persistent_keepalive=vpn_keepalive;
#if CONFIG_ETH_UPLINK
    wg_config.netif_key="ETH_DEF";
#endif
    *result=esp_wireguard_init(&wg_config,&wg_ctx);
    if (*result!=ESP_OK) return;
    wg_initialized=true;
    *result=esp_wireguard_connect(&wg_ctx);
    if (*result==ESP_OK && vpn_route_all) *result=esp_wireguard_set_default(&wg_ctx);
    if (*result!=ESP_OK) { stop_core(NULL); return; }
    wg_ctx.netif->mtu=vpn_mtu;
    ap_mss_clamp=vpn_mtu-40; ap_pmtu=vpn_mtu;
    vpn_tunnel_ip=ip_2_ip4(&wg_ctx.netif->ip_addr)->addr;
    vpn_connected=true;
    delete_portmap_tab(); apply_portmap_tab();
}
esp_err_t vpn_connect(void) { return vpn_connect_task_start() ? ESP_OK : ESP_ERR_INVALID_STATE; }
bool vpn_connect_task_start(void) {
    if (!worker) return false;
    xTaskNotifyGive(worker); return true;
}
void init_sntp_if_needed(void) {
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0,"pool.ntp.org");
        esp_sntp_setservername(1,"time.google.com"); esp_sntp_init();
    }
}
void vpn_connect_task(void *unused) {
    uint32_t attempted=UINT32_MAX;
    int64_t retry_at=0, handshake_at=0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(500));
        lock();
        bool want=using_profile ? desired : vpn_enabled;
        uint32_t token=generation;
        if (!want || !ap_connect) { unlock(); continue; }
        if (vpn_connected) {
            tcpip_callback_wait(read_peer,NULL);
            state=peer_ready ? "connected" : "handshake_pending";
            if (peer_ready) last_error=ESP_OK;
            else if (esp_timer_get_time()-handshake_at>30000000) {
                last_error=ESP_ERR_TIMEOUT; state="handshake_timeout";
            }
            unlock(); continue;
        }
        if (attempted==token && esp_timer_get_time()<retry_at) { unlock(); continue; }
        time_t now; time(&now);
        if (now<=1577836800) { state="waiting_time"; unlock(); continue; }
        char endpoint[128]; snprintf(endpoint,sizeof(endpoint),"%s",vpn_endpoint ? vpn_endpoint : "");
        state="resolving"; unlock();
        struct addrinfo hints={0}, *res=NULL; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_DGRAM;
        int dns_result=getaddrinfo(endpoint,NULL,&hints,&res);
        char address[16]="";
        if (!dns_result && res) inet_ntop(AF_INET,&((struct sockaddr_in *)res->ai_addr)->sin_addr,address,sizeof(address));
        if (res) freeaddrinfo(res);
        lock();
        /* Stop/switch invalidates a DNS result from the previous generation. */
        if (token!=generation || !(using_profile ? desired : vpn_enabled) || !ap_connect) { unlock(); continue; }
        attempted=token; retry_at=esp_timer_get_time()+10000000;
        if (!address[0]) { state="dns_error"; last_error=ESP_FAIL; unlock(); continue; }
        strcpy(resolved_endpoint,address); esp_err_t err=ESP_FAIL;
        if (tcpip_callback_wait(connect_core,&err)!=ERR_OK) err=ESP_FAIL;
        last_error=err; state=err==ESP_OK ? "handshake_pending" : "connect_error";
        handshake_at=esp_timer_get_time();
        ESP_LOGI(TAG,"VPN attempt: %s",state);
        unlock();
    }
}
static void release_legacy(void) {
    char **values[]={&vpn_private_key,&vpn_public_key,&vpn_preshared_key,&vpn_endpoint,&vpn_address,&vpn_netmask,&vpn_dns};
    for (unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) {
        if (*values[i]) { vpn_secret_clear(*values[i],strlen(*values[i])); free(*values[i]); *values[i]=NULL; }
    }
}
static void apply_profile(const vpn_profile_t *p) {
    if (!using_profile) release_legacy();
    active=*p; using_profile=true;
    vpn_private_key=active.private_key; vpn_public_key=active.public_key;
    vpn_preshared_key=active.preshared_key; vpn_endpoint=active.endpoint;
    vpn_address=active.address; vpn_netmask=active.netmask; vpn_dns=active.dns;
    vpn_port=active.port; vpn_keepalive=active.keepalive; vpn_mtu=active.mtu;
    vpn_enabled=1; vpn_killswitch=1; vpn_route_all=1;
    ip4_addr_t ip,mask;
    ip4addr_aton(active.address,&ip); ip4addr_aton(active.netmask,&mask);
    vpn_set_subnet(ip.addr&mask.addr,mask.addr);
}
esp_err_t vpn_profiles_boot(void) {
    control=xSemaphoreCreateMutex();
    if (!control) {
        vpn_profile_guard=true; vpn_enabled=0; state="worker_error";
        last_error=ESP_ERR_NO_MEM; return ESP_ERR_NO_MEM;
    }
    esp_err_t err=vpn_profiles_init();
    if (err==ESP_OK) {
        err=vpn_selection_load(&selected,&desired);
        if (err==ESP_OK && selected>=0) {
            vpn_profile_t p;
            err=vpn_profile_load(selected,&p);
            vpn_profile_guard=true;
            if (err==ESP_OK) { apply_profile(&p); state=desired ? "waiting_uplink" : "disconnected"; }
            else { desired=false; state="storage_error"; }
            vpn_secret_clear(&p,sizeof(p));
        } else if (err==ESP_OK) { vpn_enabled=0; state="router"; }
        else if (err==ESP_ERR_NVS_NOT_FOUND) {
            err=ESP_OK;
            vpn_profile_t p={.version=VPN_PROFILE_VERSION};
            /* Migrate one complete legacy full-tunnel configuration without
             * ever sending its keys to the console, browser or an API. */
            strcpy(p.name,"Imported VPN");
#define LEGACY(dst,src) if ((src) && strlen(src)<sizeof(p.dst)) strcpy(p.dst,src)
            LEGACY(private_key,vpn_private_key); LEGACY(public_key,vpn_public_key);
            LEGACY(preshared_key,vpn_preshared_key); LEGACY(endpoint,vpn_endpoint);
            LEGACY(address,vpn_address); LEGACY(netmask,vpn_netmask); LEGACY(dns,vpn_dns);
#undef LEGACY
            p.port=vpn_port; p.keepalive=vpn_keepalive; p.mtu=vpn_mtu;
            vpn_profile_t existing;
            esp_err_t prior=vpn_profile_load(0,&existing);
            vpn_secret_clear(&existing,sizeof(existing));
            if (prior==ESP_ERR_NVS_NOT_FOUND && vpn_route_all && vpn_profile_validate(&p)==ESP_OK) {
                bool connect=vpn_enabled!=0;
                err=vpn_profile_save(0,&p);
                if (err==ESP_OK) err=vpn_selection_save(connect ? 0 : -1,connect);
                if (err==ESP_OK && connect) {
                    selected=0; desired=true; vpn_profile_guard=true;
                    apply_profile(&p); state="waiting_uplink";
                }
            }
            vpn_secret_clear(&p,sizeof(p));
        }
    }
    if (err!=ESP_OK) { vpn_profile_guard=true; desired=false; vpn_enabled=0; state="storage_error"; last_error=err; }
    if (xTaskCreate(vpn_connect_task,"vpn_worker",4096,NULL,5,&worker)!=pdPASS) {
        last_error=ESP_ERR_NO_MEM; state="worker_error"; return ESP_ERR_NO_MEM;
    }
    return err;
}
esp_err_t vpn_profile_select(int id,bool connect) {
    if (!control || !worker) return ESP_ERR_INVALID_STATE;
    lock(); vpn_profile_t p;
    esp_err_t err=vpn_profile_load(id,&p);
    if (err==ESP_OK) {
        vpn_profile_guard=true; generation++; desired=false;
        err=stop_locked();
        if (err==ESP_OK) err=vpn_selection_save(id,connect);
        if (err==ESP_OK) {
            apply_profile(&p); selected=id; desired=connect; last_error=ESP_OK;
            state=connect ? "waiting_uplink" : "disconnected";
            vpn_refresh_ap_dns(); xTaskNotifyGive(worker);
        } else {
            vpn_enabled=0; last_error=err; state="storage_error";
        }
    }
    vpn_secret_clear(&p,sizeof(p)); unlock(); return err;
}
esp_err_t vpn_profile_stop(void) {
    if (selected<0) return ESP_ERR_INVALID_STATE;
    return vpn_profile_select(selected,false);
}
esp_err_t vpn_router_mode(void) {
    if (!control) return ESP_ERR_INVALID_STATE;
    lock(); generation++; desired=false;
    esp_err_t err=stop_locked();
    if (err==ESP_OK) err=vpn_selection_save(-1,false);
    if (err==ESP_OK) {
        selected=-1; vpn_enabled=0;
        vpn_profile_guard=false; state="router"; last_error=ESP_OK;
        vpn_refresh_ap_dns();
    } else { vpn_profile_guard=true; vpn_enabled=0; last_error=err; state="storage_error"; }
    unlock(); return err;
}
esp_err_t vpn_profile_get(int id,vpn_profile_t *p) {
    if (!control) return ESP_ERR_INVALID_STATE;
    lock(); esp_err_t err=vpn_profile_load(id,p);
    if (err==ESP_OK) { vpn_secret_clear(p->private_key,sizeof(p->private_key)); vpn_secret_clear(p->preshared_key,sizeof(p->preshared_key)); }
    unlock(); return err;
}
esp_err_t vpn_profile_import(int id,const char *name,const char *conf,int *saved_id,char *error,size_t n) {
    if (!control) return ESP_ERR_INVALID_STATE;
    vpn_profile_t p; esp_err_t err=vpn_profile_parse(conf,name,&p,error,n);
    if (err!=ESP_OK) return err;
    lock();
    if (id==-1) {
        vpn_profile_t existing;
        for (int i=0;i<VPN_PROFILE_MAX;i++) {
            esp_err_t read=vpn_profile_load(i,&existing);
            vpn_secret_clear(&existing,sizeof(existing));
            if (read==ESP_ERR_NVS_NOT_FOUND) { id=i; break; }
            if (read!=ESP_OK) { err=read; break; }
        }
        if (id==-1 && err==ESP_OK) err=ESP_ERR_NO_MEM;
    }
    if (id>=0 && id==selected) { err=ESP_ERR_INVALID_STATE; snprintf(error,n,"Select another profile or router mode before replacing this profile"); }
    if (err==ESP_OK) err=vpn_profile_save(id,&p);
    if (err==ESP_OK && saved_id) *saved_id=id;
    vpn_secret_clear(&p,sizeof(p)); unlock(); return err;
}
esp_err_t vpn_profile_update(int id,const vpn_profile_t *metadata) {
    if (!control || !metadata) return ESP_ERR_INVALID_STATE;
    lock(); vpn_profile_t p; esp_err_t err=vpn_profile_load(id,&p);
    if (err==ESP_OK) {
        if (id==selected) err=ESP_ERR_INVALID_STATE;
        else {
            memcpy(p.name,metadata->name,sizeof(p.name));
            memcpy(p.public_key,metadata->public_key,sizeof(p.public_key));
            memcpy(p.endpoint,metadata->endpoint,sizeof(p.endpoint));
            memcpy(p.address,metadata->address,sizeof(p.address));
            memcpy(p.netmask,metadata->netmask,sizeof(p.netmask));
            memcpy(p.dns,metadata->dns,sizeof(p.dns));
            p.port=metadata->port; p.keepalive=metadata->keepalive; p.mtu=metadata->mtu;
            err=vpn_profile_save(id,&p);
        }
    }
    vpn_secret_clear(&p,sizeof(p)); unlock(); return err;
}
esp_err_t vpn_profile_remove(int id) {
    if (!control) return ESP_ERR_INVALID_STATE;
    lock(); esp_err_t err=id==selected ? ESP_ERR_INVALID_STATE : vpn_profile_delete(id);
    unlock(); return err;
}
esp_err_t vpn_profile_clone(int id,const char *name,int *saved_id) {
    if (!control || !name || !name[0] || strlen(name)>=sizeof(active.name)) return ESP_ERR_INVALID_ARG;
    lock(); vpn_profile_t p;
    esp_err_t err=vpn_profile_load(id,&p); int target=-1;
    if (err==ESP_OK) {
        vpn_profile_t existing;
        for (int i=0;i<VPN_PROFILE_MAX;i++) {
            esp_err_t found=vpn_profile_load(i,&existing);
            vpn_secret_clear(&existing,sizeof(existing));
            if (found==ESP_ERR_NVS_NOT_FOUND) { target=i; break; }
            if (found!=ESP_OK) { err=found; break; }
        }
        if (target<0 && err==ESP_OK) err=ESP_ERR_NO_MEM;
        if (err==ESP_OK) { strcpy(p.name,name); err=vpn_profile_save(target,&p); }
        if (err==ESP_OK && saved_id) *saved_id=target;
    }
    vpn_secret_clear(&p,sizeof(p)); unlock(); return err;
}
