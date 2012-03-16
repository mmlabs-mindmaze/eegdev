/*
    Copyright (C) 2011-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef CROSS_SOCKET_H
#define CROSS_SOCKET_H

#ifndef HAVE_WINSOCK


#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif


static inline int sock_init_network_system(void) {return 0;}
static inline void sock_cleanup_network_system(void) {}

static inline
int sock_socket(int domain, int type, int protocol)
{
	int fd;

	// Open a socket and flag it CLOEXEC
	if ((fd = socket(domain, type|SOCK_CLOEXEC, protocol)) < 0)
		return -1;

	return fd;
}

#define sock_setsockopt setsockopt
#define sock_listen	listen
#define sock_bind	bind
#define sock_accept	accept
#define sock_connect	connect
#define sock_shutdown	shutdown


#else


#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT	0x0501
#include <windows.h>

//#undef UNICODE
//#undef _UNICODE
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>

typedef int socklen_t;
#define SHUT_RD		SD_RECEIVE
#define SHUT_WR		SD_SEND
#define SHUT_RDWR	SD_BOTH

static inline
int sock_init_network_system(void)
{
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData))
		return -1;
	
	return 0;
}

static inline
void sock_cleanup_network_system(void)
{
	WSACleanup();
}


static inline
int sock_socket(int domain, int type, int protocol)
{
	SOCKET s;
	int fd;

	s = WSASocket(domain, type, protocol, NULL, 0, 0);
	if (s == INVALID_SOCKET)
		return -1;
	
	fd = _open_osfhandle(s, _O_RDWR|_O_BINARY);
	if (fd == -1)
		closesocket(s);
	
	return fd;
}


static inline
int sock_setsockopt(int sockfd, int level, int name,
                    const void *val, socklen_t len)
{
	return setsockopt(_get_osfhandle(sockfd), level, name, val, len);
}


static inline
int sock_listen(int sockfd, int backlog)
{
	return listen(_get_osfhandle(sockfd), backlog);
}


static inline
int sock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	return bind(_get_osfhandle(sockfd), addr, addrlen);
}


static inline
int sock_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	SOCKET s;
	int fd;

	s = accept(_get_osfhandle(sockfd), addr, addrlen);
	if (s == INVALID_SOCKET)
		return -1;
	
	fd = _open_osfhandle(s, _O_RDWR|_O_BINARY);
	if (fd == -1)
		closesocket(s);
	
	return fd;
}


static inline
int sock_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	return connect(_get_osfhandle(sockfd), addr, addrlen);
}


static inline
int sock_shutdown(int sockfd, int how)
{
	return shutdown(_get_osfhandle(sockfd), how);
}

#endif

#endif
