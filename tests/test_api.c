#define main run_controller_tests
#include "test_controller.c"
#undef main
#include "cJSON.h"
typedef struct { size_t content_len,read; const char *body; char response[8192]; int status; } httpd_req_t;
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_RESP_USE_STRLEN -1
#define HTTP_GET 0
#define HTTP_POST 1
static bool password_set=true,authenticated=true,csrf_ok=true,header_ok=true;
static bool is_web_password_set(void){return password_set;}
static bool is_authenticated(httpd_req_t *r){return authenticated;}
static bool check_csrf(httpd_req_t *r){return csrf_ok;}
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *k,char *v,size_t n){if(!header_ok)return ESP_FAIL;strcpy(v,"1");return ESP_OK;}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *msg){r->status=status;snprintf(r->response,sizeof(r->response),"%s",msg);return ESP_OK;}
static void httpd_resp_set_type(httpd_req_t *r,const char *type){}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *val){}
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *text,int n){r->status=200;snprintf(r->response,sizeof(r->response),"%s",text);return ESP_OK;}
static int httpd_req_recv(httpd_req_t *r,char *buffer,size_t n){if(n>37)n=37;memcpy(buffer,r->body+r->read,n);r->read+=n;return (int)n;}
static unsigned esp_get_free_heap_size(void){return 100000;}
#include "../components/http_server/vpn_profiles_api.h"
static httpd_req_t post(const char *body){httpd_req_t r={0};r.body=body;r.content_len=strlen(body);assert(vpn_profiles_post_handler(&r)==ESP_OK);return r;}
static bool response_ok(httpd_req_t *r){cJSON *j=cJSON_Parse(r->response);assert(j);bool ok=cJSON_IsTrue(cJSON_GetObjectItem(j,"ok"));cJSON_Delete(j);return ok;}
int main(void){
    run_controller_tests();
    httpd_req_t r={0};password_set=false;vpn_profiles_get_handler(&r);assert(r.status==403);
    password_set=true;authenticated=false;r=post("{\"action\":\"router\"}");assert(r.status==403);
    authenticated=true;csrf_ok=false;r=post("{\"action\":\"router\"}");assert(r.status==403);
    csrf_ok=true;header_ok=false;r=post("{\"action\":\"router\"}");assert(r.status==403);
    header_ok=true;r=post("{\"action\":\"connect\",\"id\":0}");assert(response_ok(&r));
    r=(httpd_req_t){0};vpn_profiles_get_handler(&r);assert(r.status==200 && response_ok(&r));
    assert(!strstr(r.response,"private_key") && !strstr(r.response,"preshared_key") && !strstr(r.response,"[Interface]"));
    r=post("{\"action\":\"connect\",\"id\":0.5}");assert(!response_ok(&r));
    r=post("{\"action\":\"delete\",\"id\":0}");assert(!response_ok(&r));
    r=post("{\"action\":\"save\",\"id\":0,\"name\":\"Edited\"}");assert(!response_ok(&r));
    r=post("{\"action\":\"disconnect\"}");assert(response_ok(&r) && vpn_profile_guard);
    r=post("{\"action\":\"router\"}");assert(response_ok(&r) && !vpn_profile_guard);
    r=post("{\"action\":\"save\",\"id\":0,\"name\":\"Edited\",\"port\":1234}");assert(response_ok(&r));
    vpn_profile_t p;assert(vpn_profile_load(0,&p)==ESP_OK && !strcmp(p.name,"Edited") && p.port==1234 && !strcmp(p.private_key,TEST_KEY));
    r=post("{\"action\":\"save\",\"id\":0,\"port\":-1}");assert(!response_ok(&r));
    r=post("not json");assert(r.status==400);
    r=post("{\"action\":\"save\",\"id\":-1,\"name\":\"Invalid\",\"conf\":\"PrivateKey = SECRET_CANARY\"}");assert(!response_ok(&r) && !strstr(r.response,"SECRET_CANARY"));
    r=post("{\"action\":\"delete\",\"id\":0}");assert(response_ok(&r));
    puts("HTTP API tests passed: auth, CSRF, custom header, redaction, fragmented reads, validation, active-delete guard, edit preserves private key.");return 0;
}
