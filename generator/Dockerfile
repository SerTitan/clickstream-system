FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ make ca-certificates \
    librdkafka-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j \
 && test -f /app/build/clickstream-generator

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    librdkafka1 \
    curl \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://dl.min.io/client/mc/release/linux-amd64/mc -o /usr/local/bin/mc \
 && chmod +x /usr/local/bin/mc \
 && mc --version || true

COPY --from=build /app/build/clickstream-generator /usr/local/bin/generator

ENTRYPOINT ["generator"]
CMD ["--help"]
