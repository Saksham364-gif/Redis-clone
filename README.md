# Redis Clone (C++)

A Redis server built from scratch in C++ — implementing the real RESP wire protocol, core data structures, TTL expiration, authentication, and an epoll-based event loop for handling many concurrent clients on a single thread.

This isn't a toy that mimics Redis commands. It speaks the actual Redis protocol, meaning `redis-cli` and standard Redis client libraries connect to it and work correctly, without modification.

## Features

- **RESP protocol** — both array (`redis-cli`) and inline (`redis-benchmark`) formats
- **Data types** — strings, lists, hashes
- **Key expiration** — `EXPIRE`, `TTL`, `PERSIST`
- **Authentication** — password-protected via `AUTH`
- **Persistence** — append-only file (AOF), data survives restarts
- **Concurrency** — epoll-based non-blocking event loop, handles many simultaneous clients on one thread
- **Input validation** — bounded parsing, survives malformed/malicious input without crashing

## Supported Commands

`PING` `ECHO` `SET` `GET` `DEL` `EXISTS` `EXPIRE` `TTL` `PERSIST` `LPUSH` `RPUSH` `LLEN` `LRANGE` `HSET` `HGET` `HGETALL` `HDEL` `AUTH`

## Getting Started

### Requirements
- Linux (or WSL2 on Windows) - uses the Linux-specific epoll API
- g++ with C++17 support

### Build
    make

### Run
    ./my_redis_server --requirepass yourpassword

Runs on 127.0.0.1:6380 by default. Omit --requirepass to run without authentication (not recommended).

### Connect
Works with the standard redis-cli:

    redis-cli -p 6380 -a yourpassword

## Architecture

    src/
    server.cpp       - epoll event loop, connection handling
    resp.cpp         - RESP protocol parser/encoder
    storage.cpp      - in-memory data store
    commands.cpp     - command handlers
    persistence.cpp  - AOF read/write

The server uses a single-threaded, non-blocking event loop (epoll) - the same core architecture real Redis uses - rather than spawning a thread per connection. Each client's partial/incomplete commands are buffered and reassembled correctly even when TCP delivers them in fragments.

## Benchmarks

Tested with redis-benchmark, 100,000 requests per command, running inside WSL2 (numbers are lower than native Linux due to WSL2's virtualized network layer):

    PING    ~32,700 req/sec
    SET     ~38,900 req/sec
    GET     ~33,700 req/sec
    LPUSH   ~40,200 req/sec
    RPUSH   ~41,000 req/sec
    HSET    ~40,200 req/sec

## What's Not Implemented (Yet)

- Additional data types (Sets, Sorted Sets)
- Pub/Sub
- Replication
- RDB snapshotting (only AOF persistence currently)

## License

MIT
