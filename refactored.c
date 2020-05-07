#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "9034"   // port we're listening on

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int handle_new_connection(int fdmax, int listener, fd_set *master) {

    char remoteIP[INET6_ADDRSTRLEN];
    socklen_t addrlen;
    struct sockaddr_storage remoteaddr; // client address
    int newfd;        // newly accept()ed socket descriptor
    // handle new connections
    addrlen = sizeof remoteaddr;
    newfd = accept(listener,
	(struct sockaddr *)&remoteaddr,
	&addrlen);

    if (newfd == -1) {
	perror("accept");
    } else {
	FD_SET(newfd, master); // add to master set
	if (newfd > fdmax) {    // keep track of the max
	    fdmax = newfd;
	}
	printf("selectserver: new connection from %s on "
	    "socket %d\n",
	    inet_ntop(remoteaddr.ss_family,
		get_in_addr((struct sockaddr*)&remoteaddr),
		remoteIP, INET6_ADDRSTRLEN),
	    newfd);
	return fdmax;
    }
}

int handle_error(int i, int *nbytes, fd_set *master) {
	// got error or connection closed by client
	if (*nbytes == 0) {
	    // connection closed
	    printf("selectserver: socket %d hung up\n", i);
	} else {
	    perror("recv");
	}
	close(i); // bye!
	FD_CLR(i, master); // remove from master set

}

int handle_data(int i, int fdmax, fd_set *master, int listener) {

    int j;
    int rv;
    char buf[256];    // buffer for client data
    int nbytes;
    // handle data from a client
    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
	    handle_error(i, &nbytes, master);
    } else {
	// we got some data from a client
	for(j = 0; j <= fdmax; j++) {
	    // send to everyone!
	    if (FD_ISSET(j, master)) {
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

int main(void)
{
    int rv;

    int i;
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor



    int yes=1;        // for setsockopt() SO_REUSEADDR, below

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

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

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
	    
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
			// server has a new connection
			fdmax = handle_new_connection(fdmax, listener, &master);
                } else {
			handle_data(i, fdmax, &master, listener);
                } // END handle data from client
            } 
        } 
    } 
    
    return 0;
}
