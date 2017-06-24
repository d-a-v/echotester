
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
#include <assert.h>

#define DEFAULTPORT 6969 // spin round
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
	
	static int displayed = 0;
	if (!displayed)
	{
		displayed = 1;
		printf("connected to %s.\n", servername);
	}
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
	       "-f	flush input before start\n"
	       "-R	responder (read and send back)\n"
	       "-C	comparator (send and check back)\n"
	       "\n"
	       "Comparator specifics:\n"
	       "-c n	use this char instead of random data\n"
	       "-c -1	increasing data from 0\n"
	       "-s n	size (instead of infinite)\n"
	       "-s -n	random size in [1..n]\n"
	       "-w n	pause output to ensure sizesent-sizerecv < n\n"
	       "\n"
	       "Serial:\n"
	       "-y tty	use tty device\n"
	       "-b baud	for tty device\n"
	       "-m 8n1	for tty device\n"
	       "\n"
	       "TCP:\n"
	       "-n	set TCP_NODELAY option\n"
	       "-p n	set tcp port (default %i)\n"
	       "\n"
	       "TCP client:\n"
	       "-r repeat (with -s)\n"
	       "-d host	set tcp remote host name\n"
	       "(otherwise act as TCP server)\n"
	       "\n", DEFAULTPORT);
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

void echocomparator (int sock, int datasize, ssize_t maxdiff)
{
	// bufout is already filled and not modified
	// send bufout again and again
	// verify bufin receives same data at same (offset mod bufsize)
	
	setcntl(sock, F_SETFL, O_NONBLOCK, "O_NONBLOCK");
	
	static long long repeat_recvd = 0;
	long long recvd_for_avg = 0;
	long long total_sent = 0, total_recvd = 0;
	int ptr_to_send = 0;
	int ptr_for_bufout_compare = 0;
	struct pollfd pollfd;
	static struct timeval tb, ti, te;
	
	if (datasize < 0)
		datasize = (random() % -datasize) + 1;
	
	if (!repeat_recvd)
		gettimeofday(&tb, NULL);
	ti = tb;
	
	int cont = 1;
	pollfd.fd = sock;
	while (cont)
	{
		pollfd.events = POLLIN;
		if (!maxdiff || total_recvd > total_sent - maxdiff)
			pollfd.events |= POLLOUT;
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
			ssize_t bufin_offset = 0;
			while (ret)
			{
				ssize_t size = ret;
				if (size > BUFLEN - ptr_for_bufout_compare)
					size = BUFLEN - ptr_for_bufout_compare;
				if (memcmp(bufin + bufin_offset, bufout + ptr_for_bufout_compare, size) != 0)
				{
					fprintf(stderr, "\ndata differ (sent=%lli revcd=%lli ptrsend=%i ptr_for_bufout_compare=%i ret=%i size=%i)\n", total_sent, total_recvd, ptr_to_send, ptr_for_bufout_compare, (int)ret, (int)size);
					
					int i;
					for (i = 0; i < size; i++)
						if (bufin[i + bufin_offset] != bufout[i + ptr_for_bufout_compare])
						{
							printf("offset-diff @%lli @0x%llx\n", i + total_recvd, i + total_recvd);
							break;
						}
					#define SHOW 16
					// difference is at bufin[i + bufin_offset] and bufout[i + ptr_for_bufout_compare]
					// show SHOW before
					ssize_t start = i - SHOW;
					for (ssize_t j = start + BUFLEN; j < i + BUFLEN; j++)
						printf("@%llx:R%02x/S%02x\n", j + total_recvd - BUFLEN, (uint8_t)bufout[(j + ptr_for_bufout_compare) & (BUFLEN - 1)], (uint8_t)bufout[(j + ptr_for_bufout_compare) & (BUFLEN - 1)]);
					// show SHOW after
					for (ssize_t j = i; j < i + SHOW && j + bufin_offset < size; j++)
						printf("@%llx:R%02x/S%02x (diff)\n", j + total_recvd, (uint8_t)bufin[j + bufin_offset], (uint8_t)bufout[(j + ptr_for_bufout_compare) & (BUFLEN - 1)]);
					printf("\n");
					
					abort();
				}
				total_recvd += size;
				repeat_recvd += size;
				recvd_for_avg += size;
				ptr_for_bufout_compare = (ptr_for_bufout_compare + size) & (BUFLEN - 1);
				ret -= size;
				bufin_offset += size;
			}
		}
		
		if (pollfd.revents & POLLOUT)
		{
			ssize_t size = BUFLEN - ptr_to_send;
			if (datasize && (total_sent + size > datasize))
				size = datasize - total_sent;
			if (maxdiff && size > (total_recvd - total_sent + maxdiff))
				size = total_recvd - total_sent + maxdiff;
			if (size)
			{
				ssize_t ret = write(sock, bufout + ptr_to_send, size);
				if (ret == -1)
				{
					perror("write");
					abort();
				}
				total_sent += ret;
				ptr_to_send = (ptr_to_send + ret) & (BUFLEN - 1);
			}
		}
		
		gettimeofday(&te, NULL);
		cont = !datasize || datasize > total_sent || datasize > total_recvd;
		if (!cont || te.tv_sec - ti.tv_sec > 1)
		{
			printf("\r");
			printbw(te.tv_sec - tb.tv_sec, te.tv_usec - tb.tv_usec, repeat_recvd, "avg:");
			printbw(te.tv_sec - ti.tv_sec, te.tv_usec - ti.tv_usec, recvd_for_avg, "now:");
			printsz(repeat_recvd, "size:");
			printf("-----"); fflush(stdout);
			ti = te;
			recvd_for_avg = 0;
		}
	}
	
	fprintf(stderr, "  send&received %lli / %lli bytes (%i) -- \r", total_sent, repeat_recvd, datasize);

	my_close(sock);
}

void echoresponder (int sock)
{
	setcntl(sock, F_SETFL, O_NONBLOCK, "O_NONBLOCK");
	
	int ptr_to_send = 0;
	int ptr_for_recv = 0;
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
			if (maxrecv > BUFLEN - ptr_for_recv)
				maxrecv = BUFLEN - ptr_for_recv;
			ssize_t ret = read(sock, bufin + ptr_for_recv, maxrecv);
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
			ptr_for_recv = (ptr_for_recv + ret) & (BUFLEN - 1);
		}
		
		if (pollfd.revents & POLLOUT)
		{
			ssize_t maxsend = inbuf;
			if (maxsend > BUFLEN - ptr_to_send)
				maxsend = BUFLEN - ptr_to_send;
			ssize_t ret = write(sock, bufin + ptr_to_send, maxsend);
			if (ret == -1)
			{
				perror("write");
				break;
			}
			inbuf -= ret;
			ptr_to_send = (ptr_to_send + ret) & (BUFLEN - 1);
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


	tio.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
	tio.c_cflag &= ~CSIZE;
	//tio.c_cflag |= CS8;         /* 8-bit characters */
	//tio.c_cflag &= ~PARENB;     /* no parity bit */
	//tio.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
	tio.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

	/* setup for non-canonical mode */
	tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tio.c_oflag &= ~(OPOST | ONLCR);// linux don't know that: | OXTABS | ONOEOT);

	/* fetch bytes as they become available */
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

	speed_t b = 0;
	switch (baud)
	{
	case 115200: b = B115200; break;
	case 57600: b = B57600; break;
	case 38400: b = B38400; break;
	case 19200: b = B19200; break;
	case 9600: b = B9600; break;
	case 4800: b = B4800; break;
	case 2400: b = B2400; break;
	default:
		fprintf(stderr, "invalid serial speed '%d'\n", baud);
		close(fd);
		return -1;
	}
	if (cfsetispeed(&tio, b))
	{
		fprintf(stderr, "serial/cfsetispeed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (cfsetospeed(&tio, b))
	{
		fprintf(stderr, "serial/cfsetospeed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
		
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
	int port = DEFAULTPORT;
	int responder = 0;
	int comparator = 0;
	int i;
	int userchar = 0;
	int datasize = 0;
	int nodelay = 0;
	int doflushinput = 0;
	int repeat = 0;
	ssize_t maxdiff = 0;
	
	struct timeval t;
	gettimeofday(&t, NULL);
	srandom(t.tv_sec + t.tv_usec);

	while ((op = getopt(argc, argv, "hp:d:fRc:s:Cy:b:m:nfw:r")) != EOF) switch(op)
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
		
		case 'n':
			nodelay = 1;
			break;

		case 'f':
			doflushinput = 1;
			break;

		case 'R':
			responder = 1;
			break;
		
		case 'C':
			comparator = 1;
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
		
		case 'w':
			maxdiff = atoi(optarg);
			break;
		
		case 'r':
			repeat = 1;
			break;
		
		default:
			printf("option '%c' not recognized\n", op);
			help();
			return 1;
	}
	
	if (!(!!responder ^ !!comparator))
	{
		fprintf(stderr, "error: need one and only one of -R (responder) or -C (comparator) option\n\n");
		help();
		return 1;
	}
	
	if (repeat && (!datasize || !comparator))
	{
		fprintf(stderr, "use -C & -s with -r\n");
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
			fprintf(stderr, "tcp client or serial?\n");
			exit(1);
		}
		
		int fd = serial_open(tty, ttyspeed, ttymode);
		if (fd == -1)
			exit(1);
		
		if (responder)
			echoresponder(fd);
		else
		{
			if (doflushinput && !flushinput(fd))
				return 1;
			echocomparator(fd, datasize, maxdiff);
			fprintf(stderr, "\n");
		}
	}

	
	if (host)
	{

		printf("remote host:	%s\n"
		       "port:		%i\n",
		       host, port);
	
	do
	{
		int sock = my_socket();
		if (nodelay)
			setflag(sock, -1, IPPROTO_TCP, TCP_NODELAY, 1, "TCP_NODELAY");
		my_connect(host, port, sock);
		if (responder)
			echoresponder(sock);
		else
		{
				if (doflushinput && !flushinput(sock))
					return 1;
				echocomparator(sock, datasize, maxdiff);
		}
	} while (repeat);
		fprintf(stderr, "\n");
	}
	else
	{
		int sock = my_socket();
		my_bind_listen(sock, port);
		while (1)
		{
			printf("waiting on port %i\n", port);
			int clisock = my_accept(sock);
			if (nodelay)
				setflag(clisock, -1, IPPROTO_TCP, TCP_NODELAY, 1, "TCP_NODELAY");
			if (responder)
				echoresponder(clisock);
			else // comparator
			{
				if (doflushinput && !flushinput(clisock))
					return 1;
				echocomparator(clisock, datasize, maxdiff);
				fprintf(stderr, "\n");
			}
		}
		close(sock);
	}
	
	return 0;
}
