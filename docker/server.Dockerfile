FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build nlohmann-json3-dev libmysqlcppconn-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=OFF -DBUILD_QT_CLIENT=OFF -DBUILD_LOAD_TEST=OFF \
    && cmake --build /build --target chatroom_server

FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libmysqlcppconn7v5 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /build/src/server/chatroom_server /usr/local/bin/chatroom_server
COPY config/server.docker.json /etc/chatroom/server.json
USER nobody
EXPOSE 7777
ENTRYPOINT ["chatroom_server", "--config", "/etc/chatroom/server.json"]
