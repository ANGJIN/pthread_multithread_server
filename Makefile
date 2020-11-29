all : server client server_epoll

server : server.c
	gcc -o server server.c -lpthread

client : client.c
	gcc -o client client.c -lpthread

server_epoll : server_epoll.c
	gcc -o server_epoll server_epoll.c -lpthread

clean :
	rm -rf *.o server client server_epoll 
