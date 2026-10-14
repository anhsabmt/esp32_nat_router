/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define VPN_PROFILE_MAX 8
#define VPN_CONF_MAX 4096
#define VPN_PROFILE_VERSION 1
typedef struct {
    uint32_t version;
    char name[49];
    char private_key[45], public_key[45], preshared_key[45];
    char endpoint[128], address[16], netmask[16], dns[16];
    uint16_t port, keepalive, mtu;
} vpn_profile_t;
/* Parser is platform independent, never includes input text in errors. */
esp_err_t vpn_profile_parse(const char *text, const char *name,
                            vpn_profile_t *out, char *error, size_t error_len);
esp_err_t vpn_profile_validate(const vpn_profile_t *p);
void vpn_secret_clear(void *p, size_t len);
esp_err_t vpn_profiles_init(void);
esp_err_t vpn_profile_load(int id, vpn_profile_t *out);
esp_err_t vpn_profile_save(int id, const vpn_profile_t *p);
esp_err_t vpn_profile_delete(int id);
esp_err_t vpn_selection_save(int id, bool connect);
esp_err_t vpn_selection_load(int *id, bool *connect);
/* Manager serializes all public profile operations and WireGuard lifecycle. */
esp_err_t vpn_profiles_boot(void);
esp_err_t vpn_profile_import(int id, const char *name, const char *conf,
                            int *saved_id, char *error, size_t error_len);
esp_err_t vpn_profile_update(int id, const vpn_profile_t *metadata);
esp_err_t vpn_profile_remove(int id);
esp_err_t vpn_profile_select(int id, bool connect);
esp_err_t vpn_profile_stop(void);
esp_err_t vpn_router_mode(void);
esp_err_t vpn_profile_get(int id, vpn_profile_t *metadata);
int vpn_selected_profile(void);
const char *vpn_status(void);
esp_err_t vpn_last_error(void);
/* Accessed only on the TCP/IP thread by packet hooks. */
extern volatile bool vpn_profile_guard;
bool vpn_guard_ready(void);
bool vpn_guard_route(uint32_t destination);
void vpn_refresh_ap_dns(void);
