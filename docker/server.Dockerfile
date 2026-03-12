FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY cpp ./cpp
COPY CMakeLists.txt ./CMakeLists.txt
RUN cmake -S . -B build \
    && cmake --build build --target sysnetmon-server --config Release

EXPOSE 9090
CMD ["./build/sysnetmon-server", "9090"]