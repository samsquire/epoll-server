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
#include <limits.h>

#define PORT "9034"   // port we're listening on

#define SERVER "127.0.0.1"
#define MAXBUF 1024
#define MAX_EPOLL_EVENTS 64
#define MAX_EVENTS 10
#define READ 0
#define WRITE 1
#define PENDING_IO_SIZE 100

#define handle_error_en(en, msg) \
         do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
         do { perror(msg); exit(EXIT_FAILURE); } while (0)

struct io_operation {
    int conn_sock;
	int announce;
	int leave;
	int connect;
};

struct ringbuffer {
  volatile int last_head;
  volatile int last_tail;
  volatile int head;
  volatile int tail;
  
  int num_io_operations;
  int size;
  struct io_operation **operations;
  int stopped;

};

typedef struct worker_t {
	int thread_num;
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
	struct ringbuffer* ringbuffer;
    volatile int tail;
    volatile int head;
	int size;
	int num_worker_ts;
	struct worker_t* worker_ts;

} worker_t;

typedef struct client_t {
	int running;
    int epollfd;
    int nfds;
    int n;
    int listener;
    int fdmax;
    struct epoll_event ev;
    fd_set master;
	struct ringbuffer* ringbuffer;
    struct epoll_event events[MAX_EVENTS];
    volatile int tail;
    volatile int head;
	struct client_t* threads;
	int num_client_ts;
	struct ringbuffer* announces;
	int num_threads;
	struct ringbuffer* thread_ringbuffer;
	int size;
	struct client_t* client_ts;
	int thread_num;
} client_t;

void
	push_rb_client(struct ringbuffer* rb, struct client_t *client_t, struct io_operation *ptr)
	{
		struct client_t *tp = client_t;
		/*
		 * Request next place to push.
		 *
		 * Second assignemnt is atomic only for head shift, so there is
		 * a time window in which thr_p_[tid].head = ULONG_MAX, and
		 * head could be shifted significantly by other threads,
		 * so pop() will set last_head_ to head.
		 * After that thr_p_[tid].head is setted to old head value
		 * (which is stored in local CPU register) and written by @ptr.
		 *
		 * First assignment guaranties that pop() sees values for
		 * head and thr_p_[tid].head not greater that they will be
		 * after the second assignment with head shift.
		 *
		 * Loads and stores are not reordered with locked instructions,
		 * se we don't need a memory barrier here.
		 */
		tp->head = rb->head;
    
		tp->head = __atomic_fetch_add(&rb->head, 1, __ATOMIC_SEQ_CST);
    tp->head = tp->head % client_t->size;
    rb->head = rb->head % client_t->size;

		/*
		 * We do not know when a consumer uses the pop()'ed pointer,
		 * se we can not overwrite it and have to wait the lowest tail.
		 */
		while (__builtin_expect(tp->head >= (rb->last_tail + rb->size), 0))
		{
		  printf("Blocking during push..\n");
      
			int min = rb->tail;
      // printf("%d min %d tail %d last tail\n", min, tp->head, rb->last_tail);

			// Update the last_tail_.
			for (size_t i = 0; i < client_t->num_client_ts; ++i) {
				int tmp_t = client_t->client_ts[i].tail;

				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_t < min) {
					min = tmp_t;
        }
			}
			rb->last_tail = min;

			if (tp->head < (rb->last_tail + rb->size)) {
        
				break;
		    }
			
		}
    // free(&tp->operations[tp->head]);
    // printf("Pushing operation\n");

    rb->operations[tp->head] = ptr;
    // printf("Successfully pushed");
		// Allow consumers eat the item.
		tp->head = INT_MAX;
	}

void
	push_rb(struct ringbuffer* rb, struct worker_t *worker_t, struct io_operation *ptr)
	{
		struct worker_t *tp = worker_t;
		/*
		 * Request next place to push.
		 *
		 * Second assignemnt is atomic only for head shift, so there is
		 * a time window in which thr_p_[tid].head = ULONG_MAX, and
		 * head could be shifted significantly by other threads,
		 * so pop() will set last_head_ to head.
		 * After that thr_p_[tid].head is setted to old head value
		 * (which is stored in local CPU register) and written by @ptr.
		 *
		 * First assignment guaranties that pop() sees values for
		 * head and thr_p_[tid].head not greater that they will be
		 * after the second assignment with head shift.
		 *
		 * Loads and stores are not reordered with locked instructions,
		 * se we don't need a memory barrier here.
		 */
		tp->head = rb->head;
    
		tp->head = __atomic_fetch_add(&rb->head, 1, __ATOMIC_SEQ_CST);
    tp->head = tp->head % worker_t->size;
    rb->head = rb->head % worker_t->size;

		/*
		 * We do not know when a consumer uses the pop()'ed pointer,
		 * se we can not overwrite it and have to wait the lowest tail.
		 */
		while (__builtin_expect(tp->head >= (rb->last_tail + rb->size), 0))
		{
		  printf("Blocking during push..\n");
      
			int min = rb->tail;
      // printf("%d min %d tail %d last tail\n", min, tp->head, rb->last_tail);

			// Update the last_tail_.
			for (size_t i = 0; i < worker_t->num_worker_ts; ++i) {
				int tmp_t = worker_t->worker_ts[i].tail;

				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_t < min) {
					min = tmp_t;
        }
			}
			rb->last_tail = min;

			if (tp->head < (rb->last_tail + rb->size)) {
        
				break;
		    }
			
		}
    // free(&tp->operations[tp->head]);
    // printf("Pushing operation\n");

    rb->operations[tp->head] = ptr;
    // printf("Successfully pushed");
		// Allow consumers eat the item.
		tp->head = INT_MAX;
	}

void
	push_client(struct client_t *client_t, struct io_operation *ptr)
	{
		printf("Pushing item to ringbuffer\n");
		struct client_t *tp = client_t;
    struct ringbuffer *rb = tp->ringbuffer;
		/*
		 * Request next place to push.
		 *
		 * Second assignemnt is atomic only for head shift, so there is
		 * a time window in which thr_p_[tid].head = ULONG_MAX, and
		 * head could be shifted significantly by other threads,
		 * so pop() will set last_head_ to head.
		 * After that thr_p_[tid].head is setted to old head value
		 * (which is stored in local CPU register) and written by @ptr.
		 *
		 * First assignment guaranties that pop() sees values for
		 * head and thr_p_[tid].head not greater that they will be
		 * after the second assignment with head shift.
		 *
		 * Loads and stores are not reordered with locked instructions,
		 * se we don't need a memory barrier here.
		 */
		tp->head = rb->head;
    
		tp->head = __atomic_fetch_add(&rb->head, 1, __ATOMIC_SEQ_CST);
    tp->head = tp->head % client_t->size;
    rb->head = rb->head % client_t->size;

		/*
		 * We do not know when a consumer uses the pop()'ed pointer,
		 * se we can not overwrite it and have to wait the lowest tail.
		 */
		while (__builtin_expect(tp->head >= (rb->last_tail + rb->size), 0))
		{
		  printf("Blocking during push..\n");
      
			int min = rb->tail;
      // printf("%d min %d tail %d last tail\n", min, tp->head, rb->last_tail);

			// Update the last_tail_.
			for (size_t i = 0; i < client_t->num_client_ts; ++i) {
				int tmp_t = client_t->client_ts[i].tail;

				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_t < min) {
					min = tmp_t;
        }
			}
			rb->last_tail = min;

			if (tp->head < (rb->last_tail + rb->size)) {
        
				break;
		    }
			
		}
    // free(&tp->operations[tp->head]);
    // printf("Pushing operation\n");

    rb->operations[tp->head] = ptr;
    // printf("Successfully pushed");
		// Allow consumers eat the item.
		tp->head = INT_MAX;
	}

void
	push(struct worker_t *worker_t, struct io_operation *ptr)
	{
		printf("Pushing item to ringbuffer\n");
		struct worker_t *tp = worker_t;
    struct ringbuffer *rb = tp->ringbuffer;
		/*
		 * Request next place to push.
		 *
		 * Second assignemnt is atomic only for head shift, so there is
		 * a time window in which thr_p_[tid].head = ULONG_MAX, and
		 * head could be shifted significantly by other threads,
		 * so pop() will set last_head_ to head.
		 * After that thr_p_[tid].head is setted to old head value
		 * (which is stored in local CPU register) and written by @ptr.
		 *
		 * First assignment guaranties that pop() sees values for
		 * head and thr_p_[tid].head not greater that they will be
		 * after the second assignment with head shift.
		 *
		 * Loads and stores are not reordered with locked instructions,
		 * se we don't need a memory barrier here.
		 */
		tp->head = rb->head;
    
		tp->head = __atomic_fetch_add(&rb->head, 1, __ATOMIC_SEQ_CST);
    tp->head = tp->head % worker_t->size;
    rb->head = rb->head % worker_t->size;

		/*
		 * We do not know when a consumer uses the pop()'ed pointer,
		 * se we can not overwrite it and have to wait the lowest tail.
		 */
		while (__builtin_expect(tp->head >= (rb->last_tail + rb->size), 0))
		{
		  printf("Blocking during push..\n");
      
			int min = rb->tail;
      // printf("%d min %d tail %d last tail\n", min, tp->head, rb->last_tail);

			// Update the last_tail_.
			for (size_t i = 0; i < worker_t->num_worker_ts; ++i) {
				int tmp_t = worker_t->worker_ts[i].tail;

				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_t < min) {
					min = tmp_t;
        }
			}
			rb->last_tail = min;

			if (tp->head < (rb->last_tail + rb->size)) {
        
				break;
		    }
			
		}
    // free(&tp->operations[tp->head]);
    // printf("Pushing operation\n");

    rb->operations[tp->head] = ptr;
    // printf("Successfully pushed");
		// Allow consumers eat the item.
		tp->head = INT_MAX;
	}

// def pop
struct io_operation*
pop(struct client_t* client_t)
	{
		
		struct client_t *tp = client_t;
    struct ringbuffer *rb = client_t->ringbuffer;
		/*
		 * Request next place from which to pop.
		 * See comments for push().
		 *
		 * Loads and stores are not reordered with locked instructions,
		 * se we don't need a memory barrier here.
		 */
		tp->tail = rb->tail;
    // printf("Got tail \n");
		tp->tail = __atomic_fetch_add(&rb->tail, 1, __ATOMIC_SEQ_CST);
    tp->tail = tp->tail % client_t->ringbuffer->size;
    rb->tail = rb->tail % client_t->ringbuffer->size;
		/*
		 * tid'th place in ptr_array_ is reserved by the thread -
		 * this place shall never be rewritten by push() and
		 * last_tail_ at push() is a guarantee.
		 * last_head_ guaraties that no any consumer eats the item
		 * before producer reserved the position writes to it.
		 */
		while (__builtin_expect(tp->tail >= (rb->last_head), 0))
		{
      // printf("Blocking during pop %d %d..\n", tp->tail, rb->last_head);
			int min = rb->head;
    // printf("%d min %d tail %d last head\n", min, client_t->tail, rb->last_head);
			// Update the last_head_.
			for (int i = 0; i < client_t->num_client_ts; ++i) {
        
				int tmp_h = client_t->threads[i].head;
      
				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_h < min) {
					min = tmp_h;
          }
			}
			rb->last_head = min;

			if (tp->tail < (rb->last_head)) {
        
				break;
        	}

			if (rb->stopped) { return -1; }

		}
		struct io_operation *ret = (tp->ringbuffer->operations[tp->tail]);
		printf("Returning popped item %d\n", tp->tail);
		// Allow producers rewrite the slot.
		tp->tail = INT_MAX;
		return ret;
	}

// def pop_nonblocking
struct io_operation*
pop_nonblocking(struct client_t* client_t)
	{
		
		struct client_t *tp = client_t;
    struct ringbuffer *rb = client_t->ringbuffer;
		/*
		 * Request next place from which to pop.
		 * See comments for push().
		 *
		 * Loads and stores are not reordered with locked instructions,
		 * se we don't need a memory barrier here.
		 */
		tp->tail = rb->tail;
    // printf("Got tail \n");
		tp->tail = __atomic_fetch_add(&rb->tail, 1, __ATOMIC_SEQ_CST);
    tp->tail = tp->tail % client_t->ringbuffer->size;
    rb->tail = rb->tail % client_t->ringbuffer->size;
		// printf("%d %d\n", tp->tail, rb->last_head);
		while (__builtin_expect(tp->tail >= (rb->last_head), 0))
		{
      // printf("Blocking during pop %d %d..\n", tp->tail, rb->last_head);
			int min = rb->head;
    // printf("%d min %d tail %d last head\n", min, client_t->tail, rb->last_head);
			// Update the last_head_.
			for (int i = 0; i < client_t->num_client_ts; ++i) {
        
				int tmp_h = client_t->threads[i].head;
      
				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_h < min) {
					min = tmp_h;
          }
			}
			rb->last_head = min;

			if (tp->tail < (rb->last_head)) {
        
				break;
        	}

			if (rb->stopped) { return -1; }
			break;

		}
		
		if (__builtin_expect(tp->tail >= (rb->last_head), 0)) {
			return -1;
		}

		struct io_operation *ret = (rb->operations[tp->tail]);
		printf("Returning popped item %d\n", tp->tail);
		// Allow producers rewrite the slot.
		tp->tail = INT_MAX;
		return ret;
	}

// def pop_nonblocking_rb
struct io_operation*
pop_nonblocking_rb(struct ringbuffer* rb, struct client_t* client_t)
	{
		struct client_t *tp = client_t;
		tp->tail = rb->tail;
		int unclaimed = rb->tail - rb->head;
		printf("%d\n", unclaimed);
		if (abs(unclaimed) > 0) {
				// printf("Empty\n");
			// printf("Got tail \n");
			tp->tail = __atomic_fetch_add(&rb->tail, 1, __ATOMIC_SEQ_CST);
			tp->tail = tp->tail % client_t->ringbuffer->size;
			rb->tail = rb->tail % client_t->ringbuffer->size;
		} else {
			return -1;
		}
			// printf("%d %d", tp->tail, rb->last_head);
		while (__builtin_expect(tp->tail >= (rb->last_head), 0))
		{
      // printf("Blocking during pop %d %d..\n", tp->tail, rb->last_head);
			int min = rb->head;
    // printf("%d min %d tail %d last head\n", min, client_t->tail, rb->last_head);
			// Update the last_head_.
			for (int i = 0; i < client_t->num_client_ts; ++i) {
        
				int tmp_h = client_t->threads[i].head;
      
				// Force compiler to use tmp_h exactly once.
				asm volatile("" ::: "memory");

				if (tmp_h < min) {
					min = tmp_h;
          }
			}
			rb->last_head = min;

			if (tp->tail < (rb->last_head)) {
        
				break;
        	}

			if (rb->stopped) { return -1; }
			break;

		}
		if (__builtin_expect(tp->tail >= (rb->last_head), 0)) {
			return -1;
		}
		

		struct io_operation *ret = (rb->operations[tp->tail]);
		printf("Returning popped item %d\n", tp->tail);
		// Allow producers rewrite the slot.
		tp->tail = INT_MAX;
		return ret;
	}

int do_use_fd(struct client_t* client_t, int epollfd, struct epoll_event ev, int i, fd_set master, int *fdmax, int listener, int num_threads) {
	printf("Waiting for message from socket\n");
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
		printf("Receive failed");
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = i;
       if (epoll_ctl(epollfd, EPOLL_CTL_DEL, i, &ev) == -1) {
    	   perror("epoll_ctl: conn_sock");
    	   exit(EXIT_FAILURE);
       }

	close(i); // bye!
	FD_CLR(i, &master); // remove from master set
	/** Announce to all available threads that there is a new socket leaving **/
	struct io_operation* io_operation = calloc(1, sizeof(struct io_operation));
	io_operation->leave = 1;
	io_operation->conn_sock = i;
	for (int i = 0 ; i < num_threads; i++) {
		push_rb_client(&client_t->announces[i], client_t, io_operation);
	}
	return -1;
    } else {
		// we got some data from a client
		for (int j = 0; j <= *fdmax; j++) {
			// send to everyone!
			if (FD_ISSET(j, &master)) {
			printf("Sending data to client %d\n", j);
			// except the listener and ourselves
			if (j != i) {
				if (send(j, buf, nbytes, 0) == -1) {
				perror("send");
				}
			}
			}
		}
	return 0;
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


void * run_client(void * args) {
	client_t* client_t = args;
	int n = 0;
	int nfds = 0;
	int connected = 0;
	int listener = -1;
	int fdmax = -1;
	struct epoll_event ev;
	int epollfd;
    fd_set master;    // master file descriptor list
	while (client_t->running) {
		/** First we check if any other threads have new sockets */
		struct io_operation* new_socket = pop_nonblocking_rb(client_t->thread_ringbuffer, args);
		if (new_socket != -1) {
			if (new_socket->announce == 1) {
			   // printf("Socket joining thread\n");
			   FD_SET(new_socket->conn_sock, &master);
			   if (new_socket->conn_sock > fdmax) {
				  fdmax = new_socket->conn_sock;
			   }
			   continue;
			}
			if (new_socket->leave == 1) {
				printf("Socket leaving thread\n");
				FD_CLR(new_socket->conn_sock, &master); // remove from master set
			}
			
			if (new_socket->connect == 1) {
				printf("Thread received connection");	
				int conn_sock = new_socket->conn_sock;	
				epollfd = epoll_create1(0);
				if (epollfd == -1) {
				   perror("new client epoll_create1");
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
			   FD_SET(conn_sock, &master);
			   connected++;
			   struct io_operation* announce = calloc(1, sizeof(struct io_operation));
			   announce->conn_sock = conn_sock;
			   announce->announce = 1;
			   printf("There is a new socket push\n");
			   // push_client(client_t, announce);

			   for (int i = 0 ; i < client_t->num_threads; i++) {
					if (i != client_t->thread_num) {
						push_rb_client(&client_t->announces[i], client_t, announce);
					}
			   }
			   printf("There is a new socket pushed\n");
		   }
		} else if (connected > 0) {
		
		   nfds = epoll_wait(epollfd, client_t->events, MAX_EVENTS, 100);
		   if (nfds == -1) {
		   perror("epoll_wait");
		   exit(EXIT_FAILURE);
		   }

		   for (n = 0; n < nfds; ++n) {
			 if (!do_use_fd(client_t, epollfd, ev, client_t->events[n].data.fd, master, &fdmax, listener, client_t->num_threads)) {
				connected--;
			 };

		   }

		}		
	}
}

void * run_server(void * args) {
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
	int connections_per_thread = 0;
	int connections = 0;
	int initial_threads = 5;


	struct ringbuffer* io_ringbuffers = calloc(initial_threads, sizeof(struct ringbuffer));
	for (int i = 0 ; i < initial_threads; i++) {
	   struct ringbuffer* io_ringbuffer = &io_ringbuffers[i];
	   io_ringbuffer->size = PENDING_IO_SIZE;
	   io_ringbuffer->last_head = -1;
	   io_ringbuffer->last_tail = -1;
	   io_ringbuffer->tail = 0;
	   io_ringbuffer->head = 0;
	   struct io_operation **operations = calloc(100, sizeof(*operations));
	   io_ringbuffer->operations = operations;
		 if (operations == NULL) {
			 handle_error("calloc per thread ringbuffer");
		 }
	}

	struct client_t* client_t = calloc(initial_threads, sizeof(struct client_t));
	for (int i = 0 ; i < initial_threads; i++) {
	   struct client_t* args = &client_t[i];
	   pthread_t client;
	   args->thread_num = i;
	   args->running = 1;
	   args->fdmax = fdmax;
	   args->num_client_ts = initial_threads;
	   args->num_threads = initial_threads;
	   args->listener = listener;
	   args->ringbuffer = worker_t->ringbuffer;
	   args->announces = io_ringbuffers;
	   args->thread_ringbuffer = &io_ringbuffers[i];
	   args->threads = client_t;
	   args->size = PENDING_IO_SIZE;
	   args->client_ts = client_t;
	   args->num_client_ts = initial_threads;
	   args->head = INT_MAX;
	   args->tail = INT_MAX;
	   pthread_create(&client, NULL, run_client, args);
	}

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
		   /*
	       if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
			   &ev) == -1) {
		   perror("epoll_ctl: conn_sock");
		   exit(EXIT_FAILURE);
	       }
		*/
		   int connections_per_thread = 5;
		   int max_connections = initial_threads * connections_per_thread; 
		   if (connections > max_connections) {
		     printf("Too many connections\n");
		   }
	       printf("%d is the new socket\n", conn_sock);
		   struct io_operation* io_operation = calloc(1, sizeof(struct io_operation)); 
		   io_operation->conn_sock = conn_sock; 
		   io_operation->leave = -1;
		   io_operation->announce = -1;
		   io_operation->connect = 1;
		   push_rb(&io_ringbuffers[connections % initial_threads], worker_t, io_operation); // tell assigned thread to begin servicing the socket
		   printf("Pushed new socket event\n");
		   connections++;
	   } else {
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
	struct ringbuffer *io_ringbuffer = calloc(1, sizeof(struct ringbuffer));
	   io_ringbuffer->size = PENDING_IO_SIZE;
	   io_ringbuffer->last_head = -1;
	   io_ringbuffer->last_tail = -1;
	   io_ringbuffer->tail = 0;
	   io_ringbuffer->head = 0;
	   struct io_operation **operations = calloc(100, sizeof(*operations));
	   io_ringbuffer->operations = operations;
		 if (operations == NULL) {
			 handle_error("calloc operations");
		 }

   pthread_t worker;
   printf("%d", sizeof (struct worker_t));
   worker_t *args = malloc(sizeof (struct worker_t));
   args->thread_num = 1;
   args->epollfd = epollfd;
   args->remoteaddr = remoteaddr;
   args->fdmax = fdmax;
   args->listener = listener;
   args->ev = ev;
   args->master = master;
   args->ringbuffer = io_ringbuffer;
   args->size = PENDING_IO_SIZE;
   args->num_worker_ts = 1;
   args->worker_ts = args;
   args->head = INT_MAX;
   args->tail = INT_MAX;
   pthread_create(&worker, NULL, run_server, args);
   pthread_join(worker, NULL);

}
