FROM ubuntu:16.04

MAINTAINER yaechanKim <burgund32@gmail.com>

RUN apt-get update && \
    apt-get -y install gcc cmake && \
    apt-get clean

WORKDIR /server

## Copy your source files
COPY . .

## and then Compile
RUN make

# This is exposed port from container side
# You have to use "-p {host port}:{container port}"
# to connect this open port
EXPOSE 8080

## For Keeping Container From Foreground Empty
CMD ["tail", "-f", "/dev/null"]

## Or Else run server directly
# CMD ["서버 실행 파일"]