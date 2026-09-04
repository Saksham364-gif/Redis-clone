FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y g++ make
WORKDIR /build
COPY include/ include/
COPY src/ src/
COPY Makefile .
RUN make

FROM ubuntu:22.04
WORKDIR /app
COPY --from=builder /build/my_redis_server .
EXPOSE 6380
ENV REDIS_BIND=0.0.0.0
CMD ["./my_redis_server"]