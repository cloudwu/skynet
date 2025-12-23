#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#undef FD_SETSIZE
#define FD_SETSIZE 1024

#include <winsock2.h>
#include <windows.h>
#include <conio.h>

#include <ws2ipdef.h>
#include <ws2tcpip.h>

#include "socket_poll.h"
#include "socket_epoll.h"