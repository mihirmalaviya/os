#pragma once
#include <stdint.h>
#include "net/tcp.h"

int socket(void);
int bind(int fd, uint16_t port);
int listen(int fd);
int accept(int fd);
int connect(int fd, uint32_t ip, uint16_t port);
int connect_nb(int fd, uint32_t ip, uint16_t port);

tcpcb_t *sock_tcb(int fd); // NULL if fd isnt a connected socket
