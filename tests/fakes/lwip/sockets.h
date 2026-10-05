#pragma once
#ifdef _WIN32
#include <winsock2.h>
using socklen_t = int;
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <cstdint>
#include <cstddef>
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x800
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif

int fakeSocket(int, int, int);
int fakeClose(int);
int fakeFcntl(int, int, int);
int fakeBind(int, const sockaddr *, socklen_t);
int fakeListen(int, int);
int fakeAccept(int, sockaddr *, socklen_t *);
int fakeConnect(int, const sockaddr *, socklen_t);
int fakeSelect(int, fd_set *, fd_set *, fd_set *, timeval *);
int fakeGetSockOpt(int, int, int, void *, socklen_t *);
int fakeSetSockOpt(int, int, int, const void *, socklen_t);
int fakeShutdown(int, int);
int fakeRecv(int, void *, size_t, int);
int fakeSend(int, const void *, size_t, int);
int fakeRecvFrom(int, void *, size_t, int, sockaddr *, socklen_t *);
int fakeSendTo(int, const void *, size_t, int, const sockaddr *, socklen_t);
inline uint16_t fakeHtons(uint16_t n) { return (n << 8) | (n >> 8); }

#define socket fakeSocket
#define close fakeClose
#define fcntl fakeFcntl
#define bind fakeBind
#define listen fakeListen
#define accept fakeAccept
#define connect fakeConnect
#define select fakeSelect
#define getsockopt fakeGetSockOpt
#define setsockopt fakeSetSockOpt
#define shutdown fakeShutdown
#define recv fakeRecv
#define send fakeSend
#define recvfrom fakeRecvFrom
#define sendto fakeSendTo
#undef htons
#define htons fakeHtons
