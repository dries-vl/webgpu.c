/* Minimal UDP client using Windows sockets, compatible with TinyCC */
/* Minimal UDP Client for Windows */
#include <windows.h>
#include <stdio.h>

#ifdef __TINYC__
    typedef unsigned int SOCKET;
    #define INVALID_SOCKET ((SOCKET)(~0))
    #define SOCKET_ERROR (-1)
    #define AF_INET 2
    #define SOCK_DGRAM 2
    #define IPPROTO_UDP 17

    struct addrinfo {
        int ai_flags;             // Input flags
        int ai_family;            // Address family (AF_INET, AF_INET6, AF_UNSPEC)
        int ai_socktype;          // Socket type (SOCK_STREAM, SOCK_DGRAM)
        int ai_protocol;          // Protocol (IPPROTO_TCP, IPPROTO_UDP, etc.)
        size_t ai_addrlen;        // Length of the sockaddr structure
        char *ai_canonname;       // Canonical name for the host (optional)
        struct sockaddr *ai_addr; // Pointer to the sockaddr structure
        struct addrinfo *ai_next; // Pointer to next in linked list
    };

    struct sockaddr {
        unsigned short sa_family;
        char sa_data[14];
    };

    struct in_addr {
        unsigned long s_addr;
    };

    struct sockaddr_in {
        short sin_family;
        unsigned short sin_port;
        struct in_addr sin_addr;
        char sin_zero[8];
    };

    struct WSAData {
        unsigned short wVersion;
        unsigned short wHighVersion;
        char szDescription[257];
        char szSystemStatus[129];
        unsigned short iMaxSockets;
        unsigned short iMaxUdpDg;
        char *lpVendorInfo;
    };

    extern int __stdcall WSAStartup(unsigned short, struct WSAData *);
    extern int __stdcall WSACleanup(void);
    extern SOCKET __stdcall socket(int, int, int);
    extern int __stdcall sendto(SOCKET, const char *, int, int, const struct sockaddr *, int);
    extern int __stdcall recvfrom(SOCKET, char *, int, int, struct sockaddr *, int *);
    extern int __stdcall closesocket(SOCKET);
    extern int __stdcall getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
    extern void __stdcall freeaddrinfo(struct addrinfo *);
    extern unsigned short __stdcall htons(unsigned short);
    extern unsigned long __stdcall inet_addr(const char *);
#else
    #include<winsock2.h>
    #include<ws2tcpip.h>
#endif

// #define SERVER_IP "127.0.0.1"  // localhost // todo: LAN
#define SERVER_NAME "game-server.driesvl.com"  // vps // todo: secrets file in .gitignore
#define PORT 3000
#define BUF_SIZE 1024

DWORD WINAPI recv_thread(LPVOID arg) {
    SOCKET sock = (SOCKET)arg;
    struct sockaddr_in sender;
    char buf[BUF_SIZE];
    int addr_len = sizeof(sender);
    while (1) {
        int len = recvfrom(sock, buf, BUF_SIZE - 1, 0, (struct sockaddr*)&sender, &addr_len);
        if (len > 0) {
            buf[len] = '\0';
            printf("Received: %s\n", buf);
        }
    }
    return 0;
}

int main(void) {
    struct WSAData wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;

    // Set up hints for address lookup
    struct addrinfo hints, *res;
    struct sockaddr_in server_addr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // Use AF_INET for IPv4
    hints.ai_socktype = SOCK_DGRAM;  // UDP
    // Resolve the domain name
    if (getaddrinfo(SERVER_NAME, NULL, &hints, &res) != 0) {
        printf("Failed to resolve hostname\n");
        WSACleanup();
        return 1;
    }
    // Copy resolved IP address to sockaddr_in
    server_addr = *(struct sockaddr_in*)res->ai_addr;
    server_addr.sin_port = htons(PORT);
    freeaddrinfo(res);  // Free the address list

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return 1;

    CreateThread(NULL, 0, recv_thread, (LPVOID)sock, 0, NULL);
    int sent = sendto(sock, "Register client", 16, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("sendto failed");
    } else {
        printf("UDP Client connected to %s:%d\n", SERVER_NAME, PORT);
    }
    
    char buf[BUF_SIZE];
    while (fgets(buf, BUF_SIZE, stdin)) {
        int len = (int)strlen(buf);
        if (buf[len-1] == '\n') buf[len-1] = '\0';
        sendto(sock, buf, len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}

