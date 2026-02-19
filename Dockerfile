FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    qtbase5-dev \
    libfmt-dev \
    libcxxopts-dev \
    nlohmann-json3-dev \
    libvtk9-dev \
    libvtk9-qt-dev \
    pkg-config \
    bison \
    flex \
    libxmu-dev \
    libxi-dev \
    libgl-dev \
    libxt-dev \
    libsm-dev \
    libice-dev \
    libxext-dev \
    libxrender-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxinerama-dev \
    libx11-dev \
    mesa-common-dev \
    freeglut3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -Bbuild -S. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DVV_ENABLE_WARNINGS=ON \
    -DVV_WARNINGS_AS_ERRORS=ON \
    -GNinja
RUN cmake --build build --config Release -j$(nproc)

ENTRYPOINT [ "./build/vv" ]