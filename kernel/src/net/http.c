#include "net/http.h"
#include "net/socket.h"
#include "fs/vfs.h"
#include <stdint.h>
#include <stddef.h>
#include <nanoprintf.h>

int http_get(uint32_t ip, uint16_t port, const char *host, const char *path, char *buf, size_t buflen) {
    int fd = socket();
    if(connect(fd, ip, port)==-1)
        return -1;
    
    char req[256];
    int n = npf_snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    write(fd, req, n);

    int total = 0;
    int r;
    while ((r = read(fd, buf+total, buflen-total))>0){
        total+=r;
    }
    close(fd);

    return total;
}
