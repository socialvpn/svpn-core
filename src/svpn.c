
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#define MTU 1200

int translate_headers(char *buf, const char *source, const char *dest,
    const char *mac, ssize_t len);
int create_arp_response(char *buf);

typedef struct thread_opts {
    int sock;
    int tap;
    char *mac;
} thread_opts_t;

struct sockaddr_in _dest;

static int
get_dest_addr(struct sockaddr_in *dest, const char *buf, size_t len)
{
    //TODO - Implement
    memcpy(dest, &_dest, len);
    return 0;
}

static int
get_ip_addrs(const char *buf, char *source, char *dest, size_t len)
{
    //TODO - Implement
    char tmp_s[] = {172, 31, 0, 3};
    char tmp_d[] = {172, 31, 0, 2};

    memcpy(source, tmp_s, 4);
    memcpy(dest, tmp_d, 4);

    return 0;
}

static int
create_udp_socket(uint16_t port)
{
    int sock, optval = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 1) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }
    return sock;
}

static void *
udp_send_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock = opts->sock;
    int tap = opts->tap;

    ssize_t rcount;
    struct sockaddr_in dest;
    socklen_t addrlen = sizeof(dest);
    char buf[MTU];

    while (1) {

        if ((rcount = read(tap, buf, MTU)) < 0) {
            fprintf(stderr, "tap read failed\n");
            pthread_exit(NULL);
        }

        printf("read from tap %d\n", rcount);

        if (buf[12] == 0x08 && buf[13] == 0x06) {
            if (create_arp_response(buf) == 0) {
                write(tap, buf, rcount);
            }
        }
        else {
            if (get_dest_addr(&dest, buf, addrlen) < 0) {
                fprintf(stderr, "no address found\n");
                continue;
            }

            if (sendto(sock, buf, rcount, 0, (struct sockaddr*) &dest, 
                       addrlen) < 0) {
                fprintf(stderr, "sendto failed\n");
                pthread_exit(NULL);
            }
            printf("sent to udp %d\n", rcount);
        }
    }
    pthread_exit(NULL);
}

static void *
udp_recv_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock = opts->sock;
    int tap = opts->tap;

    ssize_t rcount;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buf[MTU];
    char source[4];
    char dest[4];

    while (1) {

        if ((rcount = recvfrom(sock, buf, MTU, 0, (struct sockaddr*) &addr, 
                               &addrlen)) < 0) {
            fprintf(stderr, "upd recv failed\n");
            pthread_exit(NULL);
        }

        printf("recv from udp %d\n", rcount);

        if (get_ip_addrs(buf, source, dest, rcount) < 0) {
            fprintf(stderr, "ip not found\n");
            continue;
        }

        if (translate_headers(buf, source, dest, opts->mac, rcount) < 0) {
            fprintf(stderr, "translate error\n");
            pthread_exit(NULL);
        }

        if (write(tap, buf, rcount) < 0) {
            fprintf(stderr, "write to tap error\n");
            close(tap);
            pthread_exit(NULL);
        }
        printf("wrote to tap %d\n", rcount);
    }
    pthread_exit(NULL);
}

int
main(int argc, char *argv[])
{

    memset(&_dest, 0, sizeof(_dest));
    _dest.sin_family = AF_INET;
    _dest.sin_port = htons(5800);
    _dest.sin_addr.s_addr = inet_addr(argv[1]);

    unsigned char mac[6];
    thread_opts_t opts;
    opts.mac = mac;
    opts.sock = create_udp_socket(5800);
    opts.tap = open_tap("svpn0", "172.31.0.2", mac, MTU);

    struct passwd * pwd = getpwnam("nobody");

    // drop root priviledges and set to nobody
    if (getuid() == 0) {
        if (setgid(pwd->pw_uid) < 0) {
            fprintf(stderr, "setgid failed\n");
            close(opts.tap);
            close(opts.sock);
            return -1;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.tap);
            close(opts.sock);
            return -1;
        }
    }
    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, udp_send_thread, &opts);
    pthread_create(&recv_thread, NULL, udp_recv_thread, &opts);

    while (1) {
        sleep(30);
    }
}
