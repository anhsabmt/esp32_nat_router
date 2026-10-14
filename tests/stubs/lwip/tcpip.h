#pragma once
#define ERR_OK 0
int tcpip_callback_wait(void (*fn)(void *),void *);
