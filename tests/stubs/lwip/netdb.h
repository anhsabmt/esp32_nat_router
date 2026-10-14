#pragma once
#include <stddef.h>
#define AF_INET 2
#define SOCK_DGRAM 2
struct in_addr { unsigned s_addr; };
struct sockaddr_in { struct in_addr sin_addr; };
struct addrinfo { int ai_family, ai_socktype; void *ai_addr; };
int getaddrinfo(const char *,const char *,const struct addrinfo *,struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *inet_ntop(int,const void *,char *,size_t);
