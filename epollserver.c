#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>

#define PORT "9034"   // port we're listening on

#define SERVER "127.0.0.1"
#define MAXBUF 1024
#define MAX_EPOLL_EVENTS 64
#define MAX_EVENTS 10



int do_use_fd(int epollfd, struct epoll_event ev, int i, fd_set master, int *fdmax, int listener) {
    char buf[256];    // buffer for client data
    int nbytes;
    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
	// got error or connection closed by client
	if (nbytes == 0) {
	    // connection closed
	    printf("selectserver: socket %d hung up\n", i);
	} else {
	    perror("recv");
	}

        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = i;
       if (epoll_ctl(epollfd, EPOLL_CTL_DEL, i, &ev) == -1) {
    	   perror("epoll_ctl: conn_sock");
    	   exit(EXIT_FAILURE);
       }

	close(i); // bye!
	FD_CLR(i, &master); // remove from master set

    } else {
	// we got some data from a client
	for(int j = 0; j <= *fdmax; j++) {
	    // send to everyone!
	    if (FD_ISSET(j, &master)) {
		printf("%d\n", j);
		// except the listener and ourselves
		if (listener != j && j != i) {
		    if (send(j, buf, nbytes, 0) == -1) {
			perror("send");
		    }
		}
	    }
	}
    }
}

int setunblocking(int fd)
{
    int flags;

    /* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
    /* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    /* Otherwise, use the old way of doing it */
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

typedef struct worker_t {
    int epollfd;
    int nfds;
    int n;
    int listener;
    int fdmax;
    socklen_t addrlen;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    struct sockaddr_storage remoteaddr;
    int conn_sock;
    fd_set master;
} worker_t;

void * run_worker(void * args) {
    worker_t *worker_t = args;

    int epollfd = worker_t->epollfd;
    struct epoll_event ev = worker_t->ev;
    struct epoll_event * events = worker_t->events;
    int nfds = worker_t->nfds;
    int n = worker_t->n;
    int listener = worker_t->listener;
    socklen_t addrlen = worker_t->addrlen;
    struct sockaddr_storage remoteaddr = worker_t->remoteaddr;
    int conn_sock = worker_t->conn_sock;
    int fdmax = worker_t->fdmax;
    fd_set master = worker_t->master;
   for (;;) {
       nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
       if (nfds == -1) {
	   perror("epoll_wait");
	   exit(EXIT_FAILURE);
       }

       for (n = 0; n < nfds; ++n) {
	   if (events[n].data.fd == listener) {
	       printf("Socket ready\n");
	       addrlen = sizeof remoteaddr;
	       conn_sock = accept(listener,
		(struct sockaddr *)&remoteaddr,
		&addrlen);
	       if (conn_sock == -1) {
		   perror("accept");
		   exit(EXIT_FAILURE);
	       }
	       ev.events = EPOLLIN | EPOLLET;
	       ev.data.fd = conn_sock;
	       setunblocking(conn_sock);
	       if (conn_sock > fdmax) {
			fdmax = conn_sock;
	       }
	       if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
			   &ev) == -1) {
		   perror("epoll_ctl: conn_sock");
		   exit(EXIT_FAILURE);
	       }
	       printf("%d is the new socket", conn_sock);
	       FD_SET(conn_sock, &master);
	   } else {
	       do_use_fd(epollfd, ev, events[n].data.fd, master, &fdmax, listener);
	   }
       }
   }
}

int main() {
    int sockfd;
    struct sockaddr_in dest;
    char buffer[MAXBUF];
    int i, num_ready;

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    FD_ZERO(&master);    // clear the master and temp sets

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int j, rv;

    struct addrinfo hints, *ai, *p;

    struct epoll_event ev, events[MAX_EVENTS];
    int conn_sock, nfds, epollfd;
    int n;

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }

        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }
    fdmax = listener;

    /*---Wait for socket connect to complete---*/

     epollfd = epoll_create1(0);
   if (epollfd == -1) {
       perror("epoll_create1");
       exit(EXIT_FAILURE);
   }

   ev.events = EPOLLIN;
   ev.data.fd = listener;
   if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listener, &ev) == -1) {
       perror("epoll_ctl: listen_sock");
       exit(EXIT_FAILURE);
   }

   pthread_t worker;
   worker_t *args = malloc(sizeof args);
   args->epollfd = epollfd;
   args->remoteaddr = remoteaddr;
   args->fdmax = fdmax;
   args->listener = listener;
   args->ev = ev;
   args->master = master;
   pthread_create(&worker, NULL, run_worker, args);
   pthread_join(worker, NULL);

}
