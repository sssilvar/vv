FROM debian:bullseye-slim
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    curl \
    zip \
    unzip \
    tar \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git /vcpkg
WORKDIR /vcpkg
RUN ./bootstrap-vcpkg.sh -disableMetrics

# Add deps to build
RUN apt-get update && apt-get install -y \
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

ENV VCPKG_ROOT=/vcpkg
ENV PATH="$VCPKG_ROOT:$PATH"

WORKDIR /app

# Copy vcpkg manifests and install dependencies early to cache the vcpkg_installed folder
COPY vcpkg.json vcpkg-configuration.json ./
RUN vcpkg install

COPY . .

RUN ls -la
RUN cmake --version

RUN cmake -Bbuild -S. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
    -DBUILD_SHARED_LIBS=OFF
RUN cmake --build build --config Release -j$(nproc)

ENTRYPOINT [ "./build/vv" ]