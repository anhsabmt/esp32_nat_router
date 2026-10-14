#pragma once
#define ESP_SNTP_OPMODE_POLL 0
int esp_sntp_enabled(void);
void esp_sntp_setoperatingmode(int);
void esp_sntp_setservername(int,const char *);
void esp_sntp_init(void);
