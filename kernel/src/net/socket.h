#pragma once
#include <stdint.h>

int socket(void);
int bind(int fd, uint16_t port);
int listen(int fd);
int accept(int fd);
int connect(int fd, uint32_t ip, uint16_t port);
