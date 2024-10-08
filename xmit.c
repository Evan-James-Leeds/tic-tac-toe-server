#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <string.h>

#define BUFLEN 256

int connect_inet(char *host, char *service)
{
    struct addrinfo hints, *info_list, *info;
    int sock, error;
    // look up remote host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // in practice, this means give us IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // indicate we want a streaming socket
    error = getaddrinfo(host, service, &hints, &info_list);
    if (error)
    {
        fprintf(stderr, "error looking up %s:%s: %s\n", host, service,
                gai_strerror(error));
        return -1;
    }
    for (info = info_list; info != NULL; info = info->ai_next)
    {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (sock < 0)
            continue;
        error = connect(sock, info->ai_addr, info->ai_addrlen);
        if (error)
        {
            close(sock);
            continue;
        }
        break;
    }
    freeaddrinfo(info_list);
    if (info == NULL)
    {
        fprintf(stderr, "Unable to connect to %s:%s\n", host, service);
        return -1;
    }
    return sock;
}

int main(int argc, char **argv)
{
    int sock, bytes;
    char buf[BUFLEN];
    if (argc != 3)
    {
        printf("Specify host and service\n");
        exit(EXIT_FAILURE);
    }
    sock = connect_inet(argv[1], argv[2]);
    if (sock < 0)
        exit(EXIT_FAILURE);

    while (1)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(sock + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            bytes = read(STDIN_FILENO, buf, BUFLEN);
            if (bytes <= 0)
                break;
            write(sock, buf, bytes);
        }

        if (FD_ISSET(sock, &read_fds))
        {
            bytes = read(sock, buf, BUFLEN);
            if (bytes <= 0)
                break;
            write(STDOUT_FILENO, buf, bytes);
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}
