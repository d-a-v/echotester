
// gcc -Wall -Wextra tcpechotester.c -o tcpechotester

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>

#define BUFLENLOG2 10
#define BUFLEN (1<<(BUFLENLOG2))
static char bufout [BUFLEN];
static char bufin [BUFLEN];

void getsetflag (int sock, int sock2, int level, int flag, int val, const char* name)
{
	static int ret[64];
	socklen_t retlen;
	int locval = val;
	
	retlen = 0;
	ret[0] = 0;
	if (getsockopt(sock, level, flag, ret, &retlen) == -1)
	{
		perror("getsockopt");
		abort();
	}
	printf("flag = %s(%i): %i(len=%i) - set it to %i - ", name, flag, *(int*)ret, retlen, locval);
	fflush(stdout);
	if (setsockopt(sock, level, flag, &locval, sizeof(locval)) == -1)
	{
		perror("setsockopt");
		abort();
	}
	retlen = sizeof(int);
	*(int*)ret = 0;
	if (getsockopt(sock, level, flag, &ret, &retlen) == -1)
	{
		perror("getsockopt");
		abort();
	}
	printf("re-read: %i(len=%i)\n", *(int*)ret, retlen);
	
	if (sock2 > 0 && sock2 != sock)
		getsetflag(sock2, -1, level, flag, val, name);
}
	
void setflag (int sock, int sock2, int level, int flag, int val, const char* name)
{
	int locval = val;

	printf("flag = %s(%i) - set it to %i\n", name, flag, locval);
	if (setsockopt(sock, level, flag, &locval, sizeof(locval)) == -1)
	{
		perror("setsockopt");
		abort();
	}
	if (sock2 > 0 && sock2 != sock)
		setflag(sock2, -1, level, flag, val, name);
}
	
int my_socket (void)
{
	int sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket()");
		abort();
	}
	return sock;
}

void my_bind_listen (int srvsock,  int port)
{
	struct sockaddr_in server;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(srvsock, (struct sockaddr*)&server, sizeof(server)) == -1)
	{
		perror("bind()");
		abort();
	}
	
	if (listen(srvsock, 1) == -1)
	{
		perror("listen()");
		abort();
	}
	
	printf("bind & listen done.\n");
}

int my_accept (int srvsock)
{
	int clisock;
	socklen_t n;
	struct sockaddr_in client;

	n = sizeof(client);
	if ((clisock = accept(srvsock, (struct sockaddr*)&client, &n)) == -1)
	{
		perror("accept()");
		abort();
	}
	
	printf("remote client arrived.\n");
	
	return clisock;
}

void my_connect (const char* servername,  int port,  int sock)
{
	struct sockaddr_in server;
	struct hostent *desc_server;
	
	if ((desc_server = gethostbyname(servername)) == NULL)
	{
		perror("gethostbyname()");
		abort();
	}
	
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	bcopy(desc_server->h_addr, &server.sin_addr, desc_server->h_length);
	if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == -1)
	{
		perror("connect()");
		abort();
	}
	
	printf("connected to %s.\n", servername);
}

void my_close (int sock)
{
	if (sock >= 0)
		close(sock);
}

void help (void)
{
	printf("** TCP echo tester - options are:\n"
	       "-h	this help\n"
	       "-p n	set tcp port\n"
	       "-d host	set tcp remote host name\n"
	       "-y tty	use tty device\n"
	       "-b baud	for tty device\n"
	       "-m 8n1	for tty device\n"
	       "-f	set TCP_NODELAY option\n"
	       "-S	server (server read and send back)\n"
	       "-A	active server (server sends and checks)\n"
	       "-c n	use this char instead of random data\n"
	       "-c -1	increasing data from 0\n"
	       "-s n	size (client only)\n"
	       "-s -n	random size in [1..n] (client only)\n"
	       "default: localhost:10102\n"
	       "\n");
}

long long bwbps (int diff_s, int diff_us, long long size)
{
	return size / (diff_s + 0.000001 * diff_us) * 8.0;
}

char eng (float* v)
{
	const char unit [] = ".KMGTP";
	unsigned int unitp = 0;
	while (*v > 1024 && unitp < sizeof(unit))
	{
		*v /= 1024;
		unitp++;
	}
	return unit[unitp];
}

void printbw (int diff_s, int diff_us, long long size, const char* head)
{
	float bw = bwbps(diff_s, diff_us, size);
	char u = eng(&bw);
	printf("[%s%g %cibps]", head?:"", bw, u);
}

void printsz (long long sz, const char* head)
{
	float size = sz;
	char u = eng(&size);
	printf("[%s%g %ciB]", head?:"", size, u);
}


void setcntl (int fd, int cmd, int flags, const char* name)
{
	if (fcntl(fd, cmd, flags) == -1)
		perror(name);
}

int flushinput (int sock)
{
	struct pollfd pollfd = { .fd = sock, .events = POLLIN, };
	fprintf(stderr, "flushing input...\n");
	while (1)
	{
		pollfd.events =  POLLIN;
		int ret = poll(&pollfd, 1, 1000 /*ms*/);
		
		if (ret == -1)
		{
			perror("poll");
			close(sock);
			return 0;
		}

		if (pollfd.revents & POLLIN)
		{
			char b[1024];
			ssize_t ret = read(sock, b, sizeof b);
			if (ret == -1)
			{
				perror("read");
				return 0;
			}
			if (ret == 0)
			{
				fprintf(stderr, "peer has closed\n");
				return 0;
			}
			fprintf(stderr, "flushed %i bytes\n", (int)ret);
		}
		else 
		{
			fprintf(stderr, "...done\n");
			return 1;
		}
	}
}

void echotester (int sock, int datasize)
{
	if (!flushinput(sock))
		return;
		
	// bufout is already filled and not modified
	// send bufout again and again
	// verify bufin receives same data at same (offset mod bufsize)
	
	setcntl(sock, F_SETFL, O_NONBLOCK, "O_NONBLOCK");
	
	long long sent = 0, recvd = 0, recvdnow = 0;
	int ptrsend = 0;
	int ptrrecv = 0;
	struct pollfd pollfd = { .fd = sock, .events = POLLIN | POLLOUT, };
	struct timeval tb, ti, te;
	
	if (datasize < 0)
		datasize = (random() % -datasize) + 1;
	
	gettimeofday(&tb, NULL);
	ti = tb;
	
	int cont = 1;
	while (cont)
	{
		int ret = poll(&pollfd, 1, 1000 /*ms*/);
		
		if (ret == -1)
		{
			perror("poll");
			abort();
		}

		if (pollfd.revents & POLLIN)
		{
			ssize_t ret = read(sock, bufin, BUFLEN);
			if (ret == -1)
			{
				perror("read");
				abort();
			}
printf("ret %i bytes, total read = %i\n", (int)ret, (int)recvd);
			size_t pr = 0;
			while (ret)
			{
				ssize_t size = ret;
				if (size > BUFLEN - ptrrecv)
					size = BUFLEN - ptrrecv;
				if (memcmp(bufin + pr, bufout + ptrrecv, size) != 0)
				{
					fprintf(stderr, "\ndata differ (sent=%lli revcd=%lli ptrsend=%i ptrrecv=%i ret=%i size=%i)\n", sent, recvd, ptrsend, ptrrecv, (int)ret, (int)size);
					
					int i;
					for (i = 0; i < size; i++)
						if (bufin[i + pr] != bufout[i + ptrrecv])
						{
							printf("offset-diff @%lli @0x%llx\n", i + recvd, i + recvd);
							break;
						}
printf("BUFLEN=%i size=%i bufin[i=%i + pr=%i] bufout[i=%i + ptrrecv=%i] recvdtotal=%i sent=%i\n", BUFLEN, (int)size, (int)i, (int)i, (int)pr, (int)ptrrecv, (int)recvd, (int)sent);
#if 0
					#define SHOW 16
					int j = i + pr > SHOW? i - SHOW: 0;
					int k = j + ptrrecv + SHOW + SHOW > size? size: j + SHOW + SHOW;
printf("j=%i k=%i\n", j, k);
					for (; j < k; j++)
						printf("@%llx:R%02x/S%02x\n", j + recvd, (uint8_t)bufin[j + pr], (uint8_t)bufout[j + ptrrecv]);
#else
					int j = i + pr > 16? i - 16: 0;
					//int k = ((j + 16 + (ssize_t)ptrrecv) < size)? (j + 16): size;
					int k = (i + 16 + (ssize_t)pr) <= size? (i + 16): size;
printf("j=%i k=%i\n", j, k);
					for (; j < k; j++)
						printf("@%llx:R%02x/S%02x\n", j + recvd, (uint8_t)bufin[j + pr], (uint8_t)bufout[j + ptrrecv]);
#endif
					printf("\n");
					
					abort();
				}
				recvd += size;
				recvdnow += size;
				ptrrecv = (ptrrecv + size) & (BUFLEN - 1);
				ret -= size;
				pr += size;
			}
		}
		
		if (pollfd.revents & POLLOUT)
		{
			ssize_t size = BUFLEN - ptrsend;
			if (datasize && (sent + size > datasize))
				size = datasize - sent;
			if (size)
			{
				ssize_t ret = write(sock, bufout + ptrsend, size);
				if (ret == -1)
				{
					perror("write");
					abort();
				}
				sent += ret;
				ptrsend = (ptrsend + ret) & (BUFLEN - 1);
printf("sent %i bytes, total sent = %i, ptrsend = %i\n", (int)ret, (int)sent, (int)ptrsend);
			}
		}
		
		gettimeofday(&te, NULL);
		cont = !datasize || datasize > sent || datasize > recvd;
		if (!cont || te.tv_sec - ti.tv_sec > 1)
		{
			printf("\r");
			printbw(te.tv_sec - tb.tv_sec, te.tv_usec - tb.tv_usec, recvd, "avg:");
			printbw(te.tv_sec - ti.tv_sec, te.tv_usec - ti.tv_usec, recvdnow, "now:");
			printsz(recvd, "size:");
			printf("-----"); fflush(stdout);
			ti = te;
			recvdnow = 0;
		}
	}
	
	fprintf(stderr, "\nsend&received %lli / %lli bytes (%i)\n", sent, recvd, datasize);

	my_close(sock);
}

void echoserver (int sock)
{
	setcntl(sock, F_SETFL, O_NONBLOCK, "O_NONBLOCK");
	
	int ptrsend = 0;
	int ptrrecv = 0;
	size_t inbuf = 0;
	struct pollfd pollfd = { .fd = sock, .events = POLLIN | POLLOUT, };
	
	while (1)
	{
		pollfd.events =  0;
		if (inbuf < BUFLEN) pollfd.events |= POLLIN;
		if (inbuf) pollfd.events |= POLLOUT;
		int ret = poll(&pollfd, 1, 1000 /*ms*/);
		if (ret == -1)
		{
			perror("poll");
			close(sock);
			return;
		}

		if (pollfd.revents & POLLIN)
		{
			ssize_t maxrecv = BUFLEN - inbuf;
			if (maxrecv > BUFLEN - ptrrecv)
				maxrecv = BUFLEN - ptrrecv;
			ssize_t ret = read(sock, bufin + ptrrecv, maxrecv);
			if (ret == -1)
			{
				perror("read");
				break;
			}
			if (ret == 0)
			{
				fprintf(stderr, "peer has closed\n");
				break;
			}
			inbuf += ret;
			ptrrecv = (ptrrecv + ret) & (BUFLEN - 1);
		}
		
		if (pollfd.revents & POLLOUT)
		{
			ssize_t maxsend = inbuf;
			if (maxsend > BUFLEN - ptrsend)
				maxsend = BUFLEN - ptrsend;
			ssize_t ret = write(sock, bufin + ptrsend, maxsend);
			if (ret == -1)
			{
				perror("write");
				break;
			}
			inbuf -= ret;
			ptrsend = (ptrsend + ret) & (BUFLEN - 1);
		}
		
		if (pollfd.revents & ~(POLLIN | POLLOUT))
		{
			fprintf(stderr, "unregular event occured\n");
			break;
		}
	}

	my_close(sock);
}

int serial_open (const char* dev, int baud, const char* mode)
{
	struct termios tio;

	int fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
	if (fd == -1)
	{
		fprintf(stderr, "%s: %s\n", dev, strerror(errno));
		return -1;
	}
	
	fprintf(stderr, "serial device '%s' opened, fd=%i - trying %s/%i\n", dev, fd, mode, baud);
		
	if (tcgetattr(fd, &tio) == -1)
	{
		fprintf(stderr, "serial/tcgetattr: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	tio.c_cflag = 0;

	if (cfsetospeed(&tio, (speed_t)baud) || cfsetispeed(&tio, (speed_t)baud))
	{
		fprintf(stderr, "serial/cfset(i/o)speed: %s'n", strerror(errno));
		close(fd);
		return -1;
	}

	tio.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
	tio.c_cflag &= ~CSIZE;
	//tio.c_cflag |= CS8;         /* 8-bit characters */
	//tio.c_cflag &= ~PARENB;     /* no parity bit */
	//tio.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
	tio.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

	/* setup for non-canonical mode */
	tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tio.c_oflag &= ~(OPOST | ONLCR | OXTABS | ONOEOT);

	/* fetch bytes as they become available */
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

#if 0	
	switch (baud)
	{
	case 115200: tio.c_cflag |= B115200; break;
	case 57600: tio.c_cflag |= B57600; break;
	case 38400: tio.c_cflag |= B38400; break;
	case 19200: tio.c_cflag |= B19200; break;
	case 9600: tio.c_cflag |= B9600; break;
	case 4800: tio.c_cflag |= B4800; break;
	case 2400: tio.c_cflag |= B2400; break;
	default:
		fprintf(stderr, "invalid serial speed '%d'\n", baud);
		close(fd);
		return -1;
	}
#endif
		
	switch (mode[0] - '0')
	{
	case 8: tio.c_cflag |= CS8; break;
	case 7: tio.c_cflag |= CS7; break;
	case 6: tio.c_cflag |= CS6; break;
	case 5: tio.c_cflag |= CS5; break;
	default:
		fprintf(stderr, "invalid serial data size '%c' in '%s'\n", mode[0], mode);
		close(fd);
		return -1;
	}
		
	switch (tolower(mode[1]))
	{
	case 's':
	case 'n': break;
	case 'e': tio.c_cflag |= PARENB; break;
	case 'o': tio.c_cflag |= PARENB | PARODD; break;
	default:
		fprintf(stderr, "invalid serial parity '%c' in '%s'\n", mode[1], mode);
		close(fd);
		return -1;
	}
		
	switch (mode[2] - '0')
	{
	case 1: break;
	case 2: tio.c_cflag |= CSTOPB; break;
	default:
		fprintf(stderr, "invalid serial stop bit '%c' in '%s'\n", mode[2], mode);
		close(fd); 
		return -1;
	}
		
#if 0
	tio.c_iflag = IGNPAR;
	tio.c_oflag = 0;
	tio.c_lflag = 0;   
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
#endif
		
	// empty the output queue
	if (tcflush(fd, TCIFLUSH) == -1)
	{
		fprintf(stderr, "tcflush: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	// set new attributes
	if (tcsetattr(fd, TCSANOW, &tio) == -1)
	{
		fprintf(stderr, "serial/tcsetattr: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
		
	return fd;
}

int main (int argc, char* argv[])
{
	int op;
	const char* host = NULL;
	const char* tty = NULL;
	int ttyspeed = 115200;
	const char* ttymode = "8n1";
	int port = 10102;
	int server = 0;
	int activeserver = 0;
	int i;
	int userchar = 0;
	int datasize = 0;
	//int nodelay = 0;
	
	struct timeval t;
	gettimeofday(&t, NULL);
	srandom(t.tv_sec + t.tv_usec);

	while ((op = getopt(argc, argv, "hp:d:fSc:s:Ay:b:m:")) != EOF) switch(op)
	{
		case 'h':
			help();
			return 0;

		case 'p':
			port = atoi(optarg);			
			break;
			
		case 'd':
			host = optarg;
			break;
		
//		case 'f':
//			nodelay = 1;
//			break;

		case 'S':
			server = 1;
			break;
		
		case 'A':
			activeserver = 1;
			break;
		
		case 'c':
			userchar = atoi(optarg);
			break;
		
		case 's':
			datasize = atoi(optarg);
			break;
		
		case 'y':
			tty = optarg;
			break;
		
		case 'b':
			ttyspeed = atoi(optarg);
			break;
		
		case 'm':
			ttymode = optarg;
			break;
		
		default:
			printf("option '%c' not recognized\n", op);
			help();
			return 1;
	}
	
	for (i = 0; i < BUFLEN; i++)
		switch (userchar)
		{
		case -1: bufout[i] = i; break;
		case 0: bufout[i] = random() >> 23; break;
		default: bufout[i] = userchar;
		}
		
	if (tty)
	{
		if (host)
		{
			fprintf(stderr, "tcp or serial?\n");
			exit(1);
		}
		
		int fd = serial_open(tty, ttyspeed, ttymode);
		if (fd == -1)
			exit(1);
		
		if (server)
			echoserver(fd);
		else
			echotester(fd, datasize);
	}
	else
		if (!host)
			host = "localhost";

	int sock = my_socket();
	if (server)
	{
		my_bind_listen(sock, port);
		while (1)
		{
			printf("waiting on port %i\n", port);
			int clisock = my_accept(sock);
			echoserver(clisock);
		}
		close(sock);
	}
	else if (activeserver)
	{
		my_bind_listen(sock, port);
		while (1)
		{
			printf("waiting on port %i\n", port);
			int clisock = my_accept(sock);
			echotester(clisock, datasize);
		}
		close(sock);
	}
	else
	{
		printf("remote host:	%s\n"
		       "port:		%i\n",
		       host, port);
	
		my_connect(host, port, sock);
		echotester(sock, datasize);
	}
	
	return 0;
}
