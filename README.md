# Multithreading Web Server
> #### Multithreading Web Server and Client program usnig PThreads Library

### How to Build & Run program
Please build program in linux.  
1. type `make` to build program
2. run `server` program
    * there are two kinds of `server` program. `server` and `server_epoll`
    * in this program. client program connects server address `localhost:4000` so please run this program with port number `4000`
    * ``` 
      ./server $(SERVER PORT) $(WORKER THREADS)
      example) 
      > ./server 4000 10
      create thread 0
      create thread 1
      create thread 2
      and so on...
        
      ./server_epoll $(SERVER PORT) $(WORKER THREADS)
      example)
      > ./ /server_epoll 4000 10
      create thread 0
      and so on..
      ``` 
3. run `client` program
    * there are two kinds of `client` program. `client` and `client_epoll`
    * must run `client` program corresponding to `server` program
        - if you run `server`, run `client`
        - if you run `server_epoll`, run `client_epoll`
    * ```
      ./client $(CLIENT_THREADS) $(REQUEST_PER_EACH_CLIENT_THREADS) $(PATH_OF_REQUEST_LIST_FILE)
      example)
      > ./client 10 10 ./request_list.txt
      create thread 0
      thread 0 start 1/10
      create thread 2
      and so on...
      
      ./client_epoll $(CLIENT_THREADS) $(REQUEST_PER_EACH_CLIENT_THREADS) $(PATH_OF_REQUEST_LIST_FILE)
      example)
      > ./client_epoll 10 10 ./request_list.txt
      create thread 0
      thread 0 start 1/10
      and so on...
      ```
4. type `make clean` to build clean project 