#define __STDC_WANT_LIB_EXT1__ 1

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <string.h>

#include <stdio.h>
#include "headers\main.h"

const char *const NOT_ENOUGHT_PARAMS = "Please, enter the destination IP\n";
const char *const STARTUP_MSG = "Tracing route to ";
const char *const STARTUP_MSG2 = ", sending 3 packets of size 32 to each router\n";
const char *const SENDING_FAILED = "Failed to send data, exiting...\n";
const char *const TIME_OUT = "  T/O  ";


const char *const DLL_FAILED = "DLL failed to load!\n";
const char *const SCKT_FAILED = "Failed to create RAW-socket\n";
const char *const ADDR_NOTFOUND = "Appropriate addres for connection not found\n";
const char *const ARGS_ERR = "Error in args\n";
const char *const RES_FAILED = "Bad IP\n";
const char *const CANNOT_RECIEVE = "Critical error when tried to receive data! Exiting...\n";
const char *const SELECT_FAILED = "Select failed! Exiting...\n";

HANDLE CONHANDLE;

void cleanUp(const char *msg)
{
	int res;
	printf("%s", msg);
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
		return sendRes;
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
	if ((selectRes = select(0, &singleSocket, NULL, NULL, &timeToWait)) == 0) return 0; // time-out
	if (selectRes == SOCKET_ERROR) return 1;
	
	return recvfrom(traceSckt, (char *)recvBuf, MAX_PING_PACKET_SIZE, 0, (struct sockaddr *)source, &srcLen);
}

void printPackInfo(struct sockaddr_in *source, DWORD ping, BOOL printIP)
{
	printf("%6d", ping);

	if (printIP) {
		char *srcAddr = inet_ntoa(source->sin_addr);
		if (srcAddr != NULL) {
			printf("		%s", srcAddr);
		} else {
			printf("		%s", "INVALID IP");
		}
	}
}

int decodeReply(PIPHeader ipHdr, struct sockaddr_in *source, USHORT seqNo, BOOL printIP)
{
	DWORD arrivalTime = GetTickCount();

	unsigned short ipHdrLen = (ipHdr->ver_n_len & IPHDR_LEN_MASK) * 4;
	PICMPHeader icmpHdr = (PICMPHeader)((char *)ipHdr + ipHdrLen);

	if (icmpHdr->msg_type == ICMP_TTL_EXPIRE) {
		PIPHeader requestIPHdr = (PIPHeader)((char *)icmpHdr + 4 + 4); // strange +4 bytes
		unsigned short requestIPHdrLen = (requestIPHdr->ver_n_len & IPHDR_LEN_MASK) * 4;

		PICMPHeader requestICMPHdr = (PICMPHeader)((char *)requestIPHdr + requestIPHdrLen);

		if ((requestICMPHdr->id == GetCurrentProcessId()) && (requestICMPHdr->seq == seqNo)) {
			printPackInfo(source, arrivalTime - requestICMPHdr->timestamp, printIP);
			return TRACE_TTL_EXP;
		}
	}

	if (icmpHdr->msg_type == ICMP_ECHO_REPLY) {
		if ((icmpHdr->id == GetCurrentProcessId()) && (icmpHdr->seq == seqNo)) {
			printPackInfo(source, arrivalTime - icmpHdr->timestamp, printIP);
			return TRACE_END_SUCCESS;
		}
	}

	return WRONG_PACKET;
}

int recvAndDecode(SOCKET traceSckt, PIPHeader recvBuf, struct sockaddr_in *source, long timeout, USHORT seqNo, BOOL printIP)
{
	int recvRes = recvPingResp(traceSckt, recvBuf, source, timeout);
	if (recvRes == 0) printf("%s", TIME_OUT);
	else if (recvRes == 1) printf("%s", SELECT_FAILED);
	else if (recvRes == SOCKET_ERROR) printf("%s", CANNOT_RECIEVE);
	else {
		return decodeReply(recvBuf, source, seqNo, printIP);
	}
	return (recvRes == 0 ? 0 : 3);
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("%s", NOT_ENOUGHT_PARAMS);
		return -1;
	}

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

	printf("%s", STARTUP_MSG);
	printf("%s", argv[1]);
	printf("%s", STARTUP_MSG2);

	int routerNo = 1;
	USHORT seqNo = 10;	// unsigned overflowing is OK

	BOOL traceEnd = FALSE, error = FALSE, printIP;

	do {
		printIP = FALSE;
		printf("%3d.", routerNo++);
		for (int packNo = 0; packNo < 3; packNo++) {
			if (packNo == 2) printIP = TRUE;

			initPingPacket(sendBuf, seqNo);
			if (sendPingReq(traceSckt, sendBuf, &dest) == SOCKET_ERROR) {
				printf("%s", SENDING_FAILED);
				error = TRUE;
			}
			
			int recvRes = recvAndDecode(traceSckt, recvBuf, &source, 3, seqNo, printIP);
			if (recvRes == 3) {
				error = TRUE;
				break;
			}
			int wrongCount = 0;
			while ((recvRes == WRONG_PACKET) && (wrongCount++ < 10)) recvRes = recvAndDecode(traceSckt, recvBuf, &source, 3, seqNo, printIP); // if we get 10 wrong packets, our is probably lost somewhere
			if (recvRes == WRONG_PACKET) printf("%s", TIME_OUT);
			else if (recvRes == TRACE_END_SUCCESS) traceEnd = TRUE;
			seqNo++;	
		}
		printf("\n");
		packageTTL++;
		setsockopt(traceSckt, IPPROTO_IP, IP_TTL, (char *)&packageTTL, sizeof(DWORD));
	} while (!traceEnd && !error);
	
	//BOOL destReached = FALSE, error = FALSE;
	WSACleanup();
	getchar();
	return 0;
}
