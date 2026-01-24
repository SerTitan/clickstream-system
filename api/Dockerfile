FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ make ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /app/build/api /usr/local/bin/api

ENV API_HOST=0.0.0.0
ENV API_PORT=8081
EXPOSE 8081

CMD ["api"]
