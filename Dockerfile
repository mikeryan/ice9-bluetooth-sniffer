FROM debian:bookworm-20231030 as build-stage

RUN apt-get update && \
    apt-get install -y build-essential cmake xxd libliquid-dev libhackrf-dev libbladerf-dev libuhd-dev libfftw3-dev && \
    rm -rf /var/lib/apt/lists/*

COPY . /build_dir

WORKDIR /build_dir

RUN mkdir build && \
    cd build && \
    cmake .. && \
    make

FROM scratch AS export-stage
COPY --from=build-stage /build_dir/build/ice9-bluetooth /