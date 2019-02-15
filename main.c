#define __STDC_WANT_LIB_EXT1__ 1

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <string.h>

#include "headers\main.h"

const char *const STARTUP_MSG = "Tracing route to ";
const char *const STARTUP_MSG2 = ", sending 3 packets of size 32 to each router\n";
const char *const SENDING_FAILED = "Failed to send data, exiting...\n";
const char *const TIME_OUT = "  T/O  ";


const char *const DLL_FAILED = "DLL failed to load!\n";
const char *const SCKT_FAILED = "Failed to create RAW-socket\n";
const char *const ADDR_NOTFOUND = "Appropriate addres for connection not found\n";
const char *const ARGS_ERR = "Error in args\n";
const char *const RES_FAILED = "Bad IP\n";

HANDLE CONHANDLE;

void cleanUp(const char *msg)
{
	int res;
	WriteConsole(CONHANDLE, msg, strlen(msg), &res, NULL);
	WSACleanup();
	getchar();
}

USHORT calcICMPChecksum(USHORT *packet, int size)
{
	ULONG checksum = 0;
	while (size > 1) {
		checksum += *(packet++);
		size -= sizeof(USHORT);
	}
	if (size) checksum += *(UCHAR *)packet; // MAYBE OBSOLETE!

	checksum = (checksum >> 16) + (checksum & 0xFFFF);
	checksum += (checksum >> 16);

	return (USHORT)(~checksum);
}

void initPingPacket(PICMPHeader icmp_hdr, int seqNo)
{
	icmp_hdr->msg_type = ICMP_ECHO_REQUEST;
	icmp_hdr->msg_code = 0;
	icmp_hdr->checksum = 0;
	icmp_hdr->id = LOWORD(GetCurrentProcessId());
	icmp_hdr->seq = seqNo;
	icmp_hdr->timestamp = GetTickCount(); // for getting ping time after receiving a response

	int bytesLeft = DEFAULT_PACKET_SIZE - sizeof(ICMPHeader);
	char *newData = (char *)icmp_hdr + sizeof(ICMPHeader);
	char symb = 'a';
	while (bytesLeft > 0) {
		*(newData++) = symb++;
		bytesLeft--;
	}
	icmp_hdr->checksum = calcICMPChecksum((USHORT *)icmp_hdr, DEFAULT_PACKET_SIZE);
}

int sendPingReq(SOCKET traceSckt, PICMPHeader sendBuf, const struct sockaddr_in *dest)
{
	int sendRes = sendto(traceSckt, (char *)sendBuf, DEFAULT_PACKET_SIZE, 0, (struct sockaddr *)dest, sizeof(struct sockaddr_in));

	if (sendRes == SOCKET_ERROR) {
		int err = WSAGetLastError();
		return 1;
	}
	return 0;
}

int recvPingResp(SOCKET traceSckt, PIPHeader recvBuf, struct sockaddr_in *source, long timeout)
{
	int srcLen = sizeof(struct sockaddr_in);

	fd_set singleSocket;
	singleSocket.fd_count = 1;
	singleSocket.fd_array[0] = traceSckt;

	struct timeval timeToWait = {timeout, 0};

	int selectRes;
	if ((selectRes = select(0, &singleSocket, NULL, NULL, &timeToWait)) == 0) return 1;
	
	int recvRes = recvfrom(traceSckt, (char *)recvBuf, MAX_PING_PACKET_SIZE, 0, (struct sockaddr *)source, &srcLen);

	if (recvRes == SOCKET_ERROR) {
		return 2;
	}
	return 0;
}

void printPackInfo(struct sockaddr_in *source, DWORD ping, BOOL printIP, int routerNo)
{
	int res;

	char *rNoStr = NULL;
	WriteConsole(CONHANDLE, _itoa(routerNo, rNoStr, 10), strnlen_s(rNoStr, 4), &res, NULL);

	char *pingStr = NULL;
	WriteConsole(CONHANDLE, _itoa(ping, pingStr, 10), strnlen_s(pingStr, 7), &res, NULL);

	if (printIP) {
		//struct in_addr *sourceIP = (struct in_addr *)((char *)ipHdr + IP_SOURCE_ADDR_OFFSET);
		//char *ipAddr = inet_ntoa(*sourceIP);
		char *srcAddr = inet_ntoa(source->sin_addr);
		if (srcAddr != NULL) {
			WriteConsole(CONHANDLE, srcAddr, strlen(srcAddr), &res, NULL);
			WriteConsole(CONHANDLE, "\n", 1, &res, NULL);
		} else {
			WriteConsole(CONHANDLE, "Invalid IP", strlen("Invalid IP"), &res, NULL);
		}
	}
}

int decodeReply(PIPHeader ipHdr, struct sockaddr_in *source, USHORT seqNo, BOOL printIP, int routerNo)
{
	DWORD arrivalTime = GetTickCount();

	unsigned short ipHdrLen = (ipHdr->ver_n_len & IPHDR_LEN_MASK) * 4;
	PICMPHeader icmpHdr = (PICMPHeader)((char *)ipHdr + ipHdrLen);

	if (icmpHdr->msg_type == ICMP_TTL_EXPIRE) {
		printPackInfo(source, arrivalTime - icmpHdr->timestamp, printIP, routerNo);
		if ((icmpHdr->id == GetCurrentProcessId()) && (icmpHdr->seq == seqNo)) return TRACE_TTL_EXP;
	}

	if (icmpHdr->msg_type == ICMP_ECHO_REPLY) {
		printPackInfo(source, arrivalTime - icmpHdr->timestamp, printIP, routerNo);
		if ((icmpHdr->id == GetCurrentProcessId()) && (icmpHdr->seq == seqNo)) return TRACE_END_SUCCESS;
	}

	return WRONG_PACKET;
}

int main(int argc, char *argv[])
{
	int res; // some temp var for console functions


	CONHANDLE = GetStdHandle(STD_OUTPUT_HANDLE);

	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		cleanUp(DLL_FAILED);
		return 1;
	}

	struct sockaddr_in dest, source;
	SOCKET traceSckt = WSASocket(AF_UNSPEC, SOCK_RAW, IPPROTO_ICMP, NULL, 0, 0);

	if (traceSckt == INVALID_SOCKET) {
		cleanUp(SCKT_FAILED);
		return 2;
	}
	/*******************************************/
	struct sockaddr_in src;

	src.sin_family = AF_INET;

	struct hostent *host_addr_list = gethostbyname(NULL); // deprecated function
	if(host_addr_list == NULL)
	{
		return 11; // just random nonzero return value
	}
	else
	{
	 	src.sin_addr.S_un.S_addr = *((u_long *)(host_addr_list->h_addr_list[0]));
	}

	if (bind(traceSckt, (struct sockaddr *)&src, sizeof(src)) != NO_ERROR) {
		return 12; // same
	}

	DWORD tmp, prm = RCVALL_IPLEVEL; // disabling ICMP filter
	if (WSAIoctl(traceSckt, SIO_RCVALL, &prm, sizeof(prm), NULL, 0, &tmp, NULL, NULL) == SOCKET_ERROR) {
		int err = WSAGetLastError();
		WriteConsole(CONHANDLE, "Mode change failed\n", strlen("Mode change failed\n"), &res, NULL);
		return 8;
	}
	//-----------------------------------------------


	DWORD packageTTL = 1;
	setsockopt(traceSckt, IPPROTO_IP, IP_TTL, (char *)&packageTTL, sizeof(DWORD));

	ZeroMemory(&dest, sizeof(dest));

	UINT destAddr = inet_addr(argv[1]); // getting an IP from cmd params
	if (destAddr != INADDR_NONE) {
		dest.sin_addr.s_addr = destAddr;
		dest.sin_family = AF_INET;
	} else {
		// probly a domain name; need to resolve through DNS; later
		cleanUp("DN found\n");
		return 3;
	}

	PICMPHeader sendBuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DEFAULT_PACKET_SIZE);
	PIPHeader recvBuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_PING_PACKET_SIZE);

	WriteConsole(CONHANDLE, STARTUP_MSG, strlen(STARTUP_MSG), &res, NULL);
	WriteConsole(CONHANDLE, argv[1], strlen(argv[1]), &res, NULL);
	WriteConsole(CONHANDLE, STARTUP_MSG2, strlen(STARTUP_MSG2), &res, NULL);

	int routerNo = 1;
	USHORT seqNo = 10;	// unsigned overflowing is OK

	initPingPacket(sendBuf, seqNo++);
	sendPingReq(traceSckt, sendBuf, &dest);
	int recvRes;
	recvRes = recvPingResp(traceSckt, recvBuf, &source, 3);

	//BOOL printIP = TRUE; // if it's the last 3d packet need to print an IP; will be used in final version
	//int decodeRes = decodeReply(recvBuf, &source, seqNo, printIP, routerNo);
	WriteConsole(CONHANDLE, "hi\n", strlen("hi\n"), &res, NULL);
	
	//BOOL destReached = FALSE, error = FALSE;

	getchar();
	return 0;
}
