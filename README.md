# epoll-server

This code uses epoll to check on a number of available sockets for traffic. A server thread creates 5 client threads and each client thread serves 5 clients.

The threads communicate with a multithreaded multiconsumer multiproducer ringbuffer, which signals when a new client has joined or left.

This allows the computer to scale with the number of connections and CPUs.
