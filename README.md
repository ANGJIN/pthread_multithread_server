# Multithreading Web Server
> #### Multithreading Web Server and Client program usnig PThreads Library

### How to Build & Run program
Please build program in linux.  
1. type `make` to build program
2. run `server` program
    * ``` 
      ./server $(SERVER PORT) $(WORKER THREADS)
      example) 
      > ./server 4000 10
      create thread 0
      create thread 1
      create thread 2
      and so on...  
      ```
3. run `client` program
    * ```
      ./client $(CLIENT_THREADS) $(REQUEST_PER_EACH_CLIENT_THREADS) $(PATH_OF_REQUEST_LIST_FILE)
      example)
      > ./client 10 10 ./request_list.txt
      create thread 0
      thread 0 start 1/10
      create thread 2
      and so on...
      ```
4. type `make clean` to build clean project 