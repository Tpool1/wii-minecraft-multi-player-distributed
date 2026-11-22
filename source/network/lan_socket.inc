/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Adapt nonblocking socket results: a call either returns a byte count, is
 * still pending, or fails. libogc returns negative errno directly. */
#ifdef PLATFORM_WII
#define __LINUX_ERRNO_EXTENSIONS__
#include <gccore.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <errno.h>
typedef int game_socket;
#define BAD_SOCKET (-1)
#define sock_open net_socket
#define sock_listen net_listen
#define sock_bind net_bind
#define sock_accept net_accept
#define sock_close net_close
#define sock_send net_send
#define sock_recv net_recv
#define ADDRESS_LEN 8
static int sock_connect(game_socket fd, struct sockaddr_in* a) {
	int r = net_connect(fd, (struct sockaddr*)a, ADDRESS_LEN);
	return r == -EISCONN ? 0 : r;
}
static int nonblock(game_socket fd) {
	uint32_t yes = 1;
	return net_ioctl(fd, FIONBIO, &yes);
}
static bool pending(int n) {
	return n == -EINPROGRESS || n == -EALREADY || n == -EWOULDBLOCK
		|| n == -EAGAIN || n == -EINTR;
}
static uint32_t millis(void) {
	return (uint32_t)ticks_to_millisecs(gettime());
}
static bool net_ready(void) {
	static bool started;
	if(!started) {
		int e = net_init_async(NULL, NULL);
		if(e < 0 && e != -EBUSY && e != -EALREADY)
			return false;
		started = true;
	}
	int e = net_get_status();
	if(e == -EBUSY)
		return false;
	if(e < 0)
		started = false;
	return e >= 0;
}
#elif defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
typedef SOCKET game_socket;
typedef int socklen_t;
#define BAD_SOCKET INVALID_SOCKET
#define sock_open socket
#define sock_listen listen
#define sock_bind bind
#define sock_accept accept
#define sock_close closesocket
#define sock_send send
#define sock_recv recv
#define ADDRESS_LEN sizeof(struct sockaddr_in)
static int nonblock(game_socket fd) {
	u_long yes = 1;
	return ioctlsocket(fd, FIONBIO, &yes);
}
static bool pending(int n) {
	(void)n;
	int e = WSAGetLastError();
	return e == WSAEINPROGRESS || e == WSAEALREADY || e == WSAEWOULDBLOCK
		|| e == WSAEINTR;
}
static int sock_connect(game_socket fd, struct sockaddr_in* a) {
	int n = connect(fd, (struct sockaddr*)a, ADDRESS_LEN);
	return n == SOCKET_ERROR && WSAGetLastError() == WSAEISCONN ? 0 : n;
}
static uint32_t millis(void) {
	return GetTickCount();
}
static bool net_ready(void) {
	static bool ready;
	WSADATA w;
	if(!ready)
		ready = WSAStartup(MAKEWORD(2, 2), &w) == 0;
	return ready;
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
typedef int game_socket;
#define BAD_SOCKET (-1)
#define sock_open socket
#define sock_listen listen
#define sock_bind bind
#define sock_accept accept
#define sock_close close
#define sock_recv recv
#define ADDRESS_LEN sizeof(struct sockaddr_in)
static int sock_send(game_socket fd, const void* p, size_t n, int flags) {
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	return (int)send(fd, p, n, flags);
}
static int nonblock(game_socket fd) {
	return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}
static bool pending(int n) {
	(void)n;
	return errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK
		|| errno == EAGAIN || errno == EINTR;
}
static int sock_connect(game_socket fd, struct sockaddr_in* a) {
	int n = connect(fd, (struct sockaddr*)a, ADDRESS_LEN);
	return n < 0 && errno == EISCONN ? 0 : n;
}
static uint32_t millis(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)t.tv_sec * 1000 + (uint32_t)t.tv_nsec / 1000000;
}
static bool net_ready(void) {
	return true;
}
#endif
#if defined(LAN_TEST_IO) && defined(_WIN32)
/* Executable tests can stall this process's stream without touching the OS
 * configuration. */
int lan_test_send(game_socket fd, const char* p, size_t n, int flags);
int lan_test_recv(game_socket fd, char* p, size_t n, int flags);
uint32_t lan_test_clock(void);
#undef sock_send
#undef sock_recv
#define sock_send lan_test_send
#define sock_recv lan_test_recv
#define millis lan_test_clock
#endif
