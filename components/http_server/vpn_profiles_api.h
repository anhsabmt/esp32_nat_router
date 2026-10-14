/* Included by http_server.c to share existing session and CSRF checks.
 * SPDX-License-Identifier: MIT */
#include "vpn_profiles.h"
static esp_err_t profile_reply(httpd_req_t *req,cJSON *json) {
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char *text=json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!text) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    esp_err_t err=httpd_resp_send(req,text,HTTPD_RESP_USE_STRLEN);
    free(text); return err;
}
static bool profile_authorized(httpd_req_t *req,bool write) {
    if (!is_web_password_set() || !is_authenticated(req)) {
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Set an admin password and log in on the home page first");
        return false;
    }
    char header[8];
    if (write && (!check_csrf(req) ||
        httpd_req_get_hdr_value_str(req,"X-VPN-Request",header,sizeof(header))!=ESP_OK || strcmp(header,"1"))) {
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Invalid same-origin request"); return false;
    }
    return true;
}
static cJSON *profile_metadata(int id,const vpn_profile_t *p) {
    cJSON *j=cJSON_CreateObject(); if (!j) return NULL;
    cJSON_AddNumberToObject(j,"id",id);
#define STR(f) cJSON_AddStringToObject(j,#f,p->f)
    STR(name); STR(public_key); STR(endpoint); STR(address); STR(netmask); STR(dns);
#undef STR
    cJSON_AddNumberToObject(j,"port",p->port);
    cJSON_AddNumberToObject(j,"keepalive",p->keepalive);
    cJSON_AddNumberToObject(j,"mtu",p->mtu);
    return j;
}
static esp_err_t vpn_profiles_get_handler(httpd_req_t *req) {
    if (!profile_authorized(req,false)) return ESP_OK;
    cJSON *j=cJSON_CreateObject(), *list=cJSON_CreateArray();
    if (!j || !list) { cJSON_Delete(j); cJSON_Delete(list); return profile_reply(req,NULL); }
    cJSON_AddBoolToObject(j,"ok",true);
    cJSON_AddNumberToObject(j,"selected",vpn_selected_profile());
    cJSON_AddStringToObject(j,"state",vpn_status());
    cJSON_AddNumberToObject(j,"error",vpn_last_error());
    cJSON_AddBoolToObject(j,"killswitch",vpn_profile_guard);
    cJSON_AddNumberToObject(j,"heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(j,"max",VPN_PROFILE_MAX);
    cJSON_AddItemToObject(j,"profiles",list);
    for (int id=0;id<VPN_PROFILE_MAX;id++) {
        vpn_profile_t p;
        esp_err_t err=vpn_profile_get(id,&p);
        if (err==ESP_OK) cJSON_AddItemToArray(list,profile_metadata(id,&p));
        else if (err!=ESP_ERR_NVS_NOT_FOUND) cJSON_AddNumberToObject(j,"storage_error",err);
        vpn_secret_clear(&p,sizeof(p));
    }
    return profile_reply(req,j);
}
static bool profile_string(cJSON *j,const char *field,char *dst,size_t capacity) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,field);
    if (!v) return true;
    if (!cJSON_IsString(v) || strlen(v->valuestring)>=capacity) return false;
    strcpy(dst,v->valuestring); return true;
}
static bool profile_number(cJSON *j,const char *field,uint16_t *dst,unsigned min,unsigned max) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,field);
    if (!v) return true;
    if (!cJSON_IsNumber(v) || v->valuedouble<min || v->valuedouble>max || v->valuedouble!=v->valueint) return false;
    *dst=v->valueint; return true;
}
static esp_err_t vpn_profiles_post_handler(httpd_req_t *req) {
    if (!profile_authorized(req,true)) return ESP_OK;
    if (!req->content_len || req->content_len>6144)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Body must be 1 through 6144 bytes");
    char *body=malloc(req->content_len+1);
    if (!body) return profile_reply(req,NULL);
    size_t total=0;
    while (total<req->content_len) {
        int n=httpd_req_recv(req,body+total,req->content_len-total);
        if (n<=0) { vpn_secret_clear(body,total); free(body); return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Incomplete request"); }
        total+=n;
    }
    body[total]=0;
    const char *end=NULL;
    cJSON *j=memchr(body,0,total) ? NULL : cJSON_ParseWithOpts(body,&end,true);
    vpn_secret_clear(body,total); free(body);
    if (!cJSON_IsObject(j)) { cJSON_Delete(j); return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid JSON"); }
    cJSON *action=cJSON_GetObjectItemCaseSensitive(j,"action"), *ident=cJSON_GetObjectItemCaseSensitive(j,"id");
    int id=-1, saved=-1;
    esp_err_t err=ESP_ERR_INVALID_ARG;
    char msg[160]="Invalid action or fields";
    bool valid_id=ident && cJSON_IsNumber(ident) && ident->valuedouble==ident->valueint && ident->valueint>=-1 && ident->valueint<VPN_PROFILE_MAX;
    if (valid_id) id=ident->valueint;
    if (cJSON_IsString(action)) {
        const char *a=action->valuestring;
        if (!strcmp(a,"router")) err=vpn_router_mode();
        else if (!strcmp(a,"disconnect")) err=vpn_profile_stop();
        else if (valid_id && id>=0 && (!strcmp(a,"select") || !strcmp(a,"connect"))) err=vpn_profile_select(id,!strcmp(a,"connect"));
        else if (valid_id && id>=0 && !strcmp(a,"delete")) err=vpn_profile_remove(id);
        else if (valid_id && !strcmp(a,"save")) {
            cJSON *conf=cJSON_GetObjectItemCaseSensitive(j,"conf"), *name=cJSON_GetObjectItemCaseSensitive(j,"name");
            if (cJSON_IsString(conf) && conf->valuestring[0] && cJSON_IsString(name))
                err=vpn_profile_import(id,name->valuestring,conf->valuestring,&saved,msg,sizeof(msg));
            else if (id>=0) {
                vpn_profile_t p;
                err=vpn_profile_get(id,&p);
                if (err==ESP_OK) {
                    bool ok=true;
#define UPDATE(f) ok=profile_string(j,#f,p.f,sizeof(p.f)) && ok
                    UPDATE(name); UPDATE(public_key); UPDATE(endpoint); UPDATE(address); UPDATE(netmask); UPDATE(dns);
#undef UPDATE
                    ok=profile_number(j,"port",&p.port,1,65535) && ok;
                    ok=profile_number(j,"keepalive",&p.keepalive,0,65535) && ok;
                    ok=profile_number(j,"mtu",&p.mtu,1280,1420) && ok;
                    err=ok ? vpn_profile_update(id,&p) : ESP_ERR_INVALID_ARG;
                    if (err==ESP_OK) saved=id;
                }
                vpn_secret_clear(&p,sizeof(p));
            }
        }
    }
    /* cJSON duplicates the uploaded config; wipe that allocation too. */
    cJSON *conf=cJSON_GetObjectItemCaseSensitive(j,"conf");
    if (cJSON_IsString(conf)) vpn_secret_clear(conf->valuestring,strlen(conf->valuestring));
    cJSON_Delete(j);
    if (err==ESP_OK) strcpy(msg,"Saved. Reconnect client Wi-Fi to renew DNS after switching profiles.");
    else if (err==ESP_ERR_INVALID_STATE) strcpy(msg,"Select another profile or normal router mode before editing/deleting this profile.");
    else if (err==ESP_ERR_NO_MEM) strcpy(msg,"Storage limit (8 profiles) or insufficient memory.");
    else if (err!=ESP_ERR_INVALID_ARG) strcpy(msg,"Storage or controller error. VPN traffic remains protected; try again.");
    cJSON *reply=cJSON_CreateObject();
    if (!reply) return profile_reply(req,NULL);
    cJSON_AddBoolToObject(reply,"ok",err==ESP_OK);
    cJSON_AddStringToObject(reply,"msg",msg);
    cJSON_AddNumberToObject(reply,"error",err);
    cJSON_AddNumberToObject(reply,"id",saved);
    return profile_reply(req,reply);
}
static httpd_uri_t vpn_profiles_getp={.uri="/api/vpn-profiles",.method=HTTP_GET,.handler=vpn_profiles_get_handler};
static httpd_uri_t vpn_profiles_postp={.uri="/api/vpn-profiles",.method=HTTP_POST,.handler=vpn_profiles_post_handler};
