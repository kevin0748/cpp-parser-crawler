#include "pch.h"
#include "request.h"

Request::Request(HTMLParserBase *_htmlParser) {
	urlParser = new URLParser();
	htmlParser = _htmlParser;
	sock = new Socket();

	hostAddr.S_un.S_addr = 0;
}

Request::~Request() {
	delete urlParser;
	delete sock;
}


int parseResponseStatus(char* const buf) {
	// printf("\tVerifying header... ");

	int status = 0;
	char* startOfStatus = strstr(buf, "HTTP/");
	if (startOfStatus != NULL)
	{
		char skipStr[] = "HTTP/1.X ";
		startOfStatus += strlen(skipStr);
		char statusStr[4];
		strncpy_s(statusStr, sizeof(statusStr), startOfStatus, 3);
		statusStr[3] = '\0';

		status = atoi(statusStr);
	}
	else {
		// printf("failed with non-HTTP header\n");
		return -1;
	}

	// printf("status code %d\n", status);
	return status;
}

void Request::RequestURL(string url) {
	// printf("URL: %s\n", url.c_str());

	// printf("\tParsing URL... ");
	bool ret = urlParser->parse(url);
	if (!ret) {
		return;
	}

	/*
	printf("host %s, port %d\n",
		urlParser->host.c_str(),
		urlParser->port,
		urlParser->getRequest().c_str());*/


	// check host uniqueness
	// printf("\tChecking host uniqueness... ");
	if (seenHosts.find(urlParser->host) != seenHosts.end()) {
		// printf("failed\n");
		return;
	}

	EnterCriticalSection(&hostMutex);
	seenHosts.insert(urlParser->host);
	// printf("passed\n");
	LeaveCriticalSection(&hostMutex);

	if (!DnsLookup(urlParser->host)) {
		return;
	}

	// robot
	ret = sock->Send(urlParser, hostAddr, Socket::robots);
	if (!ret) {
		return;
	}

	clock_t timer = clock();
	// printf("\tLoading... ");
	ret = sock->Read(MAX_ROBOTS_DOWNLOAD_SIZE);
	if (!ret) {
		return;
	}

	timer = clock() - timer;
	// printf("done in %d ms with %d bytes\n", 1000 * timer / CLOCKS_PER_SEC, sock->curPos);

	//printf("%s\n", sock->buf);

	int status = parseResponseStatus(sock->buf);
	if (status < 400 || status >= 500) {
		// robots.txt exists. We should skip this website.
		return;
	}

	// Create new socket to accept new connection
	delete sock;
	sock = new Socket();

	ret = sock->Send(urlParser, hostAddr, Socket::page);
	if (!ret) {
		return;
	}

	// printf("\tLoading... ");
	ret = sock->Read(MAX_PAGE_DOWNLOAD_SIZE);
	if (!ret) {
		return;
	}

	timer = clock() - timer;
	// printf("done in %d ms with %d bytes\n", 1000 * timer / CLOCKS_PER_SEC, sock->curPos);

	status = parseResponseStatus(sock->buf);
	if (status == -1) {
		return;
	}
	else if (status >= 200 && status < 300) {
		char baseUrl[512];
		sprintf_s(baseUrl, "%s://%s", urlParser->scheme.c_str(), urlParser->host.c_str());

		timer = clock();
		// printf("\t\b\b+ Parsing page... ");
		int nLinks;
		char* linkBuffer = htmlParser->Parse(sock->buf, sock->curPos, baseUrl, (int)strlen(baseUrl), &nLinks);

		// check for errors indicated by negative values
		if (nLinks < 0)
			nLinks = 0;

		timer = clock() - timer;
		// printf("done in %d ms with %d links\n", 1000 * timer / CLOCKS_PER_SEC, nLinks);
	}

	return;
}

bool Request::DnsLookup(string host) {
	if (hostAddr.S_un.S_addr != 0) {
		// dns lookup is done 
		return true;
	}

	// structure used in DNS lookups
	struct hostent* remote;

	clock_t timer = clock();
	// printf("\tDoing DNS... ");
	// first assume that the string is an IP address
	const char* hostChars = host.c_str();
	DWORD IP = inet_addr(hostChars);
	if (IP == INADDR_NONE)
	{
		// if not a valid IP, then do a DNS lookup
		if ((remote = gethostbyname(hostChars)) == NULL)
		{
			// printf("failed with %d\n", WSAGetLastError());
			//printf("Invalid string: neither FQDN, nor IP address\n");
			return false;
		}
		else // take the first IP address and copy into sin_addr
			memcpy((char*)&(hostAddr), remote->h_addr, remote->h_length);
	}
	else
	{
		// if a valid IP, directly drop its binary version into sin_addr
		hostAddr.S_un.S_addr = IP;
	}

	timer = clock() - timer;
	// printf("done in %d ms, found %s\n", 1000 * timer / CLOCKS_PER_SEC, inet_ntoa(hostAddr));

	// printf("\tChecking IP uniqueness... ");
	//printf("int: %d\n str: %s\n", hostAddr.S_un.S_addr, inet_ntoa(hostAddr));
	if (seenIPs.find(hostAddr.S_un.S_addr) != seenIPs.end()) {
		// printf("failed\n");
		return false;
	}


	EnterCriticalSection(&ipMutex);
	seenIPs.insert(hostAddr.S_un.S_addr);
	// printf("passed\n");
	LeaveCriticalSection(&ipMutex);

	return true;
}