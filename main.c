#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

#define BUF 16384 // 16kb

CRITICAL_SECTION cs;

DWORD WINAPI handle(LPVOID sock);
extern int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
extern void freeaddrinfo(struct addrinfo* res);

static int MAX_THREADS;
static int LOGGING;

static int THREADS = 0;

int main()
{
	int PORT;
	printf("Port: ");
	scanf_s("%d", &PORT);
	printf("Thread-limit (<=0 - No limit): ");
	scanf_s("%d", &MAX_THREADS);
	printf("Logging (1/0): ");
	scanf_s("%d", &LOGGING);
	printf("\n");

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		perror("WSASrartup");
		return -1;
	}

	SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener == INVALID_SOCKET)
	{
		perror("socket");
		return -1;
	}

	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(PORT);

	bind(listener, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(listener, SOMAXCONN);

	InitializeCriticalSection(&cs);

	while (1)
	{
		if (GetAsyncKeyState(VK_PAUSE))
		{
			break;
		}

		if (MAX_THREADS > 0 && THREADS >= MAX_THREADS)
		{
			continue;
		}

		SOCKET client = accept(listener, 0, 0);
		if (client == INVALID_SOCKET)
		{
			continue;
		}

		HANDLE thread = CreateThread(NULL, 0, handle, (LPVOID)client, 0, NULL);
		if (thread)
		{
			CloseHandle(thread);
		}
		else
		{
			perror("CreateThread");
			closesocket(client);
		}
	}

	DeleteCriticalSection(&cs);

	closesocket(listener);
	WSACleanup();

	return 0;
}

DWORD WINAPI handle(LPVOID sock)
{
	EnterCriticalSection(&cs);
	THREADS++;
	LeaveCriticalSection(&cs);

	SOCKET client = (SOCKET)sock;
	char* buf = (char*)malloc(BUF);
	if (buf == NULL)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		//perror("malloc");
		closesocket(client);
		return 0;
	}

	int bytesReceived = recv(client, buf, BUF - 1, 0);
	if (bytesReceived <= 0)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		free(buf);
		closesocket(client);
		return 0;
	}

	buf[bytesReceived] = '\0';

	char* tempBuf = (char*)realloc(buf, strlen(buf) + 1);
	if (tempBuf == NULL)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		//perror("realloc");
		free(buf);
		closesocket(client);
		return 0;
	}

	buf = tempBuf;
	tempBuf = NULL;

	char* connectS = strstr(buf, "CONNECT ");
	int isSecure = connectS - buf == 0;

	char* host = strstr(buf, "Host: ");
	if (host == NULL)
	{
		host = strstr(buf, "HOST: ");
		if (host == NULL)
		{
			EnterCriticalSection(&cs);
			THREADS--;
			LeaveCriticalSection(&cs);

			free(buf);
			closesocket(client);
			return 0;
		}
	}

	int hostIndex = (int)(host - buf);

	char* limit = strstr(buf + hostIndex, "\r\n");
	if (limit == NULL)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		free(buf);
		closesocket(client);
		return 0;
	}
	
	int limitIndex = (int)(limit - buf);

	int length = limitIndex - hostIndex - 6;

	char *url = (char*)malloc(length + 1), *port;
	if (url == NULL)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		//perror("malloc");
		free(buf);
		closesocket(client);
		return 0;
	}

	if (strncpy_s(url, length + 1, host + 6, length) != 0)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		free(url);
		free(buf);
		closesocket(client);
		return 0;
	}

	url[length] = '\0';

	char* portLimit = strchr(url, ':');
	if (portLimit != NULL)
	{
		*portLimit = '\0';
		int portLimitIndex = (int)(portLimit - url);
		int portLen = length - portLimitIndex;
		
		port = (char*)malloc(portLen + 1);
		if (port == NULL)
		{
			EnterCriticalSection(&cs);
			THREADS--;
			LeaveCriticalSection(&cs);

			//perror("malloc");
			free(url);
			free(buf);
			closesocket(client);
			return 0;
		}

		strncpy_s(port, portLen + 1, portLimit + 1, portLen);
		port[portLen] = '\0';
	}
	else
	{
		port = (char*)malloc(isSecure ? 4 : 3);
		if (port == NULL)
		{
			EnterCriticalSection(&cs);
			THREADS--;
			LeaveCriticalSection(&cs);

			//perror("malloc");
			free(url);
			free(buf);
			closesocket(client);
			return 0;
		}

		if (isSecure)
		{
			strcpy_s(port, 4, "443");
			port[3] = '\0';
		}
		else
		{
			strcpy_s(port, 3, "80");
			port[2] = '\0';
		}
	}
	
	if (LOGGING)
	{
		EnterCriticalSection(&cs);
		printf("%s:%s | %d\n", url, port, THREADS);
		LeaveCriticalSection(&cs);
	}

	if (url[0] == '\0')
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		free(url);
		free(port);
		free(buf);
		closesocket(client);
		return 0;
	}

	SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server == INVALID_SOCKET)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		free(url);
		free(port);
		free(buf);
		closesocket(client);
		return 0;
	}

	struct addrinfo hints, *serverInfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(url, port, &hints, &serverInfo) != 0)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		//perror("getaddrinfo");
		free(url);
		free(port);
		free(buf);
		closesocket(server);
		closesocket(client);
		return 0;
	}

	if (connect(server, serverInfo->ai_addr, (int)serverInfo->ai_addrlen) == SOCKET_ERROR)
	{
		EnterCriticalSection(&cs);
		THREADS--;
		LeaveCriticalSection(&cs);

		//perror("connect");
		free(url);
		free(port);
		free(buf);
		freeaddrinfo(serverInfo);
		closesocket(server);
		closesocket(client);
		return 0;
	}

	freeaddrinfo(serverInfo);

	if (isSecure)
	{
		char* res = "HTTP/1.1 200 Connection Established\r\n\r\n";
		send(client, res, (int)strlen(res), 0);
		
		free(buf);
		buf = (char*)malloc(BUF);
		if (buf == NULL)
		{
			EnterCriticalSection(&cs);
			THREADS--;
			LeaveCriticalSection(&cs);

			//perror("malloc");
			free(url);
			free(port);
			closesocket(server);
			closesocket(client);
			return 0;
		}

		while (1)
		{
			fd_set read_fds;
			FD_ZERO(&read_fds);
			FD_SET(client, &read_fds);
			FD_SET(server, &read_fds);

			int max = (int)client > (int)server ? (int)client : (int)server;

			if (select(max + 1, &read_fds, NULL, NULL, NULL) < 0)
			{
				break;
			}

			if (FD_ISSET(client, &read_fds))
			{
				bytesReceived = recv(client, buf, BUF - 1, 0);
				if (bytesReceived <= 0)
				{
					break;
				}

				send(server, buf, bytesReceived, 0);
			}

			if (FD_ISSET(server, &read_fds))
			{
				bytesReceived = recv(server, buf, BUF - 1, 0);
				if (bytesReceived <= 0)
				{
					break;
				}

				send(client, buf, bytesReceived, 0);
			}
		}
	}
	else
	{
		send(server, buf, (int)strlen(buf), 0);

		while (1)
		{
			bytesReceived = recv(server, buf, BUF - 1, 0);
			if (bytesReceived <= 0)
			{
				break;
			}

			send(client, buf, bytesReceived, 0);
		}
	}

	EnterCriticalSection(&cs);
	THREADS--;
	LeaveCriticalSection(&cs);

	free(url);
	free(port);
	free(buf);
	closesocket(server);
	closesocket(client);
	return 0;
}
