#ifdef WINDOZE
#pragma message( "Compiling " __FILE__ " on " __DATE__ " at " __TIME__ )
#pragma message( "File last modified on " __TIMESTAMP__ )
#pragma message( "  ")
#pragma title( "My Secret Box version 2.0" )
#pragma subtitle( "Copyright (c) 2003 - 2015, Nehemiah Ministries, Inc." )
#pragma comment( compiler )
#pragma comment( user, "File: " __FILE__ ". Compiled on " __DATE__ " at " __TIME__ ".  Last modified on " __TIMESTAMP__ )
#endif

// simples.c - An Internet Protocol server application for viewing ANSI (color) text data sent via UDP or TCP.

#ifdef WINDOZE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#endif
#ifndef WINDOZE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef bool
    #define bool int
    #define false ((bool)0)
    #define true  ((bool)1)
#endif
#define MIN(x, y) (((x) > (y)) ? (y) : (x))

#define DEF_PORT 5001
// define default protocol to be UDP
#define DEF_PROTOCOL SOCK_DGRAM
#define MAXERRORCNT		20
void printint (int *ibuf,int num);
#ifdef WINDOZE
extern int OpenCon(HANDLE *hDev);
extern int SetScreenColor( int fcolorcode, int fintensity, int bcolorcode, int bintensity );
extern int ResetScreenColor( void );
extern BOOL ParseAndPrintString( HANDLE hDev, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten );
HANDLE hDev;
#endif


void usage(char *arg) {
	fprintf(stderr,"Usage : %s -p [protocol] -e [port] -i [ipaddr]\n",
		arg);
	fprintf(stderr,"Where:\n\tprotocol is TCP or UDP\n");
	fprintf(stderr,"\tport is the port to listen on\n");
	fprintf(stderr,"\tipaddr is the ip address to bind to\n");
	fprintf(stderr,"Defaults are UDP, 5001 and INADDR_ANY\n");
#ifdef WINDOZE
	WSACleanup();
#endif
	exit(1);
}
int main(int argc, char **argv) {
#define BUFFERSIZE 50000
	int iMessagesSizes[BUFFERSIZE];
	int i;
	int flen;
	int retval;
	int ierrorcount = 0;
	int socktype = DEF_PROTOCOL;
	unsigned short port=DEF_PORT;
	char *inter= NULL;
	char buf[BUFFERSIZE];
	struct sockaddr_in local, from;
#ifdef WINDOZE
	HANDLE hStdout;
	WSADATA wsaData;
	SOCKET listensock, msgsock;
	unsigned long NumberOfBytesWritten;
#else
#define INVALID_SOCKET	-1
#define SOCKET_ERROR	-1
	int listensock, msgsock;
#endif

	// command line arguments
	if (argc >1) {
		for(i=1;i <argc;i++) {
			if ( (argv[i][0] == '-') || (argv[i][0] == '/') ) {
				switch(tolower(argv[i][1])) {

					case 'e':
					port = atoi(argv[++i]);
					break;

					case 'p':
#ifdef WINDOZE
						if (!stricmp(argv[i+1], "TCP") )
#else
						if (!strcasecmp(argv[i+1], "TCP") )
#endif
							socktype = SOCK_STREAM;
#ifdef WINDOZE
						else if (!stricmp(argv[i+1], "UDP") )
#else
						else if (!strcasecmp(argv[i+1], "UDP") )
#endif
							socktype = SOCK_DGRAM;
						else
							usage(argv[0]);
						i++;
						break;

					case 'i':
						inter = argv[++i];
						break;

					default:
						usage(argv[0]);
						break;
				}
			}
			else
				usage(argv[0]);
		}
	}

	for (i=0;i<BUFFERSIZE;i++) iMessagesSizes[i] = 0;
#ifdef WINDOZE
	// start up the winsock by process
	if (WSAStartup(0x202,&wsaData) == SOCKET_ERROR) {
		fprintf(stderr,"winsock failed to initialize, error number = %d\n",WSAGetLastError());
		WSACleanup();
		return -1;
	}
#endif
	if ( port < 1 || port > 65535){
		usage(argv[0]);
	}
loop0:
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = (!inter)?INADDR_ANY:inet_addr(inter); 
	local.sin_port = htons(port);

	listensock = socket(AF_INET, socktype,0); // connect via socktype (UDP or TCP)
	
	if (listensock == INVALID_SOCKET){
#ifdef WINDOZE
		fprintf(stderr,"socket open failed, error number = %d\n",WSAGetLastError());
		WSACleanup();
#else
		int errornumsave = errno;
		fprintf(stderr,"socket open failed, error number = %d (%s)\n",errornumsave,strerror(errornumsave));
#endif
		return -1;
	}

	if (bind(listensock,(struct sockaddr*)&local,sizeof(local) ) == SOCKET_ERROR) {
#ifdef WINDOZE
		fprintf(stderr,"IP bind failed, error number = %d\n",WSAGetLastError());
		WSACleanup();
#else
		int errornumsave = errno;
		fprintf(stderr,"IP bind failed, error number = %d (%s)\n",errornumsave,strerror(errornumsave));
#endif
		return -1;
	}

	if (socktype != SOCK_DGRAM) {
		if (listen(listensock,5) == SOCKET_ERROR) {
#ifdef WINDOZE
			fprintf(stderr,"TCP listen failed, error number = %d\n",WSAGetLastError());
			WSACleanup();
#else
			int errornumsave = errno;
			fprintf(stderr,"TCP listen failed, error number = %d (%s)\n",errornumsave,strerror(errornumsave));
#endif
			return -1;
		}
	}

#ifdef WINDOZE
	OpenCon(&hStdout);
	hDev = hStdout;
#else
	printf("\e[H\e[2J\e[1;31m");
#endif
//SetScreenColor( 4, 1, 0, 0);

	printf("Remote Debug Server Version 3.0\n");
#ifdef WINDOZE
	printf("Listening on %s port %d\n", (socktype == SOCK_STREAM)?"TCP":"UDP", port);
	ResetScreenColor();
#else
	printf("Listening on %s port %d\e[0m\n", (socktype == SOCK_STREAM)?"TCP":"UDP", port);
#endif

	while(1) {
		flen =sizeof(from);
		if (socktype != SOCK_DGRAM) {
#ifdef WINDOZE
			msgsock = accept(listensock,(struct sockaddr*)&from, &flen);
			if (msgsock == INVALID_SOCKET) {
				fprintf(stderr,"accept() error %d\n",WSAGetLastError());
				WSACleanup();
#else
			msgsock = accept(listensock,(struct sockaddr*)&from, (socklen_t *)&flen);
			if (msgsock == INVALID_SOCKET) {
				int errornumsave = errno;
				fprintf(stderr,"accept() error %d\n",errornumsave);
#endif
				return -1;
			}
			printf("connected from %s, port %d\n", inet_ntoa(from.sin_addr), htons(from.sin_port)) ;
		}
		else
			msgsock = listensock;

loop1:
		if (socktype != SOCK_DGRAM)
			retval = recv(msgsock,buf,sizeof (buf),0 );
		else {
			retval = recvfrom(msgsock,buf,sizeof (buf),0,
#ifdef WINDOZE
				(struct sockaddr *)&from,&flen);
#else
				(struct sockaddr *)&from,(socklen_t *)&flen);
#endif
		}
			
		if (retval == SOCKET_ERROR) {
#ifdef WINDOZE
			fprintf(stderr,"%s recv failed, error number = %d\n",(socktype == SOCK_STREAM)?"TCP":"UDP",WSAGetLastError());
			closesocket(msgsock);
#else
			int errornumsave = errno;
			fprintf(stderr,"%s recv failed, error number = %d (%s)\n",(socktype == SOCK_STREAM)?"TCP":"UDP",errornumsave,strerror(errornumsave));
			close(msgsock);
#endif
			if ( (ierrorcount++) < MAXERRORCNT ) {
				goto loop0;	// reest everything
			} else{
				break;	// admit defeat
			}
//			continue;
		}
		if (retval == 0) {
			printf("client dis-connected\n");
#ifdef WINDOZE
			closesocket(msgsock);
#else
			close(msgsock);
#endif
			if ( (ierrorcount++) < MAXERRORCNT ) {
				goto loop0;	// reest everything
			} else{
				break;	// admit defeat
			}
//			continue;
		}
		if (strlen(buf) < BUFFERSIZE) {
#ifdef WINDOZE
			ParseAndPrintString( hDev, buf, strlen(buf), &NumberOfBytesWritten );
#else
			printf("%s",buf);
#endif
		} else {
			buf[BUFFERSIZE-1] = '\000';
#ifdef WINDOZE
			ParseAndPrintString( hDev, buf, strlen(buf), &NumberOfBytesWritten );
#else
			printf("%s",buf);
#endif
		}
		iMessagesSizes[strlen(buf)]++;
		if (socktype != SOCK_DGRAM){
			goto loop1;
		}
		continue;
	}
	printint (iMessagesSizes, BUFFERSIZE);
	return 1;
}

void printint (int *ibuf,int num) 
{ 
	int i;
	printf(" message size statistics:\n");
	for (i=0;i<num;i++) {
		if ( ibuf[i] !=0) {
			printf("i=%i #=%i\n",i,*(ibuf+i));
			ibuf[i] = 0;
		}
	}
	return;
}

