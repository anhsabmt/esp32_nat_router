/* Bounded NVS blobs; no filesystem, list of secrets, or plaintext export.
 * SPDX-License-Identifier: MIT */
#include "vpn_profiles.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#define PART "vpn_nvs"
#define NS "profiles"
typedef struct { uint32_t version; int32_t id; uint32_t connect; } selection_t;
esp_err_t vpn_profiles_init(void) {
    /* Never erase automatically on corruption/no space: preserve credentials. */
    return nvs_flash_init_partition(PART);
}
static esp_err_t open_store(nvs_handle_t *h) { return nvs_open_from_partition(PART,NS,NVS_READWRITE,h); }
static bool id_key(int id,char *key) {
    if (id<0 || id>=VPN_PROFILE_MAX) return false;
    snprintf(key,8,"p%d",id); return true;
}
esp_err_t vpn_profile_load(int id,vpn_profile_t *p) {
    char key[8]; nvs_handle_t h;
    if (!p || !id_key(id,key)) return ESP_ERR_INVALID_ARG;
    memset(p,0,sizeof(*p));
    esp_err_t err=open_store(&h); if (err!=ESP_OK) return err;
    size_t n=sizeof(*p);
    err=nvs_get_blob(h,key,p,&n); nvs_close(h);
    if (err==ESP_OK && (n!=sizeof(*p) || vpn_profile_validate(p)!=ESP_OK)) err=ESP_ERR_INVALID_STATE;
    if (err!=ESP_OK) vpn_secret_clear(p,sizeof(*p));
    return err;
}
esp_err_t vpn_profile_save(int id,const vpn_profile_t *p) {
    char key[8]; nvs_handle_t h;
    if (!id_key(id,key) || vpn_profile_validate(p)!=ESP_OK) return ESP_ERR_INVALID_ARG;
    esp_err_t err=open_store(&h); if (err!=ESP_OK) return err;
    err=nvs_set_blob(h,key,p,sizeof(*p));
    if (err==ESP_OK) err=nvs_commit(h);
    nvs_close(h); return err;
}
esp_err_t vpn_profile_delete(int id) {
    char key[8]; nvs_handle_t h;
    if (!id_key(id,key)) return ESP_ERR_INVALID_ARG;
    esp_err_t err=open_store(&h); if (err!=ESP_OK) return err;
    err=nvs_erase_key(h,key); if (err==ESP_OK) err=nvs_commit(h);
    nvs_close(h); return err;
}
esp_err_t vpn_selection_save(int id,bool connect) {
    if (id < -1 || id>=VPN_PROFILE_MAX || (id==-1 && connect)) return ESP_ERR_INVALID_ARG;
    selection_t s={VPN_PROFILE_VERSION,id,connect}; nvs_handle_t h;
    esp_err_t err=open_store(&h); if (err!=ESP_OK) return err;
    err=nvs_set_blob(h,"selection",&s,sizeof(s)); if (err==ESP_OK) err=nvs_commit(h);
    nvs_close(h); return err;
}
esp_err_t vpn_selection_load(int *id,bool *connect) {
    nvs_handle_t h; selection_t s={0}; size_t n=sizeof(s);
    esp_err_t err=open_store(&h); if (err!=ESP_OK) return err;
    err=nvs_get_blob(h,"selection",&s,&n); nvs_close(h);
    if (err==ESP_OK && (n!=sizeof(s) || s.version!=VPN_PROFILE_VERSION || s.id < -1 ||
        s.id>=VPN_PROFILE_MAX || s.connect>1 || (s.id==-1 && s.connect))) return ESP_ERR_INVALID_STATE;
    if (err==ESP_OK) { *id=s.id; *connect=s.connect!=0; }
    return err;
}
