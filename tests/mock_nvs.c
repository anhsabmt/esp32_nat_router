#include "nvs.h"
#include <string.h>
#include <assert.h>
typedef struct { char key[16]; unsigned char value[1024]; size_t size; } entry;
static entry committed[16], pending[16];
int mock_nvs_failure;
esp_err_t nvs_flash_init_partition(const char *p) { assert(!strcmp(p,"vpn_nvs")); return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h) {
    assert(!strcmp(p,"vpn_nvs") && !strcmp(ns,"profiles")); *h=1;
    memcpy(pending,committed,sizeof(pending)); return ESP_OK;
}
static entry *find(entry *table,const char *key,int create) {
    for(int i=0;i<16;i++) if(!strcmp(table[i].key,key)) return &table[i];
    if(create) for(int i=0;i<16;i++) if(!table[i].key[0]) { strcpy(table[i].key,key); return &table[i]; }
    return NULL;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n) {
    entry *e=find(committed,key,0); if(!e) return ESP_ERR_NVS_NOT_FOUND;
    if(*n<e->size) return ESP_ERR_INVALID_ARG;
    *n=e->size; memcpy(out,e->value,*n); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t n) {
    if(mock_nvs_failure) return ESP_ERR_NO_MEM;
    entry *e=find(pending,key,1); if(!e || n>1024) return ESP_ERR_NO_MEM;
    e->size=n; memcpy(e->value,data,n); return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    entry *e=find(pending,key,0); if(!e) return ESP_ERR_NVS_NOT_FOUND;
    memset(e,0,sizeof(*e)); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    if(mock_nvs_failure) return ESP_FAIL;
    memcpy(committed,pending,sizeof(committed)); return ESP_OK;
}
void nvs_close(nvs_handle_t h) {}
void mock_nvs_corrupt(const char *key) { entry *e=find(committed,key,0); assert(e); e->size=3; }
