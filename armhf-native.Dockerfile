FROM debian:10

RUN apt-get update
RUN apt-get install -y \
        libprocps7 \
        libssl1.1 \
        libuv1
