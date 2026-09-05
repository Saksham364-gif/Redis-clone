# Redis Clone (C++)

A Redis server built from scratch in C++ - implementing the real RESP wire protocol, core data structures, TTL expiration, authentication, Pub/Sub, and an epoll-based event loop for handling many concurrent clients on a single thread.

This isn't a toy that mimics Redis commands. It speaks the actual Redis protocol, meaning redis-cli and standard Redis client libraries (Node, Python, Go, etc.) connect to it and work correctly, without modification.

## Features

- RESP protocol - both array (redis-cli) and inline (redis-benchmark) formats
- Data types - strings, lists, hashes, sets
- Key expiration - EXPIRE, TTL, PERSIST
- Pub/Sub - SUBSCRIBE, UNSUBSCRIBE, PUBLISH with real-time message delivery
- Authentication - password-protected via AUTH (CLI flag or environment variable)
- Persistence - append-only file (AOF), data survives restarts
- Concurrency - epoll-based non-blocking event loop, handles many simultaneous clients on one thread
- Input validation - bounded parsing, survives malformed/malicious input without crashing
- Docker support - run with zero local compiler setup

## Supported Commands

    PING, ECHO, AUTH
    SET, GET, DEL, EXISTS
    EXPIRE, TTL, PERSIST
    LPUSH, RPUSH, LLEN, LRANGE
    HSET, HGET, HGETALL, HDEL
    SADD, SMEMBERS, SREM, SISMEMBER, SCARD
    SUBSCRIBE, UNSUBSCRIBE, PUBLISH

## Getting Started

### Requirements

- Linux (or WSL2 on Windows) - uses the Linux-specific epoll API
- g++ with C++17 support
- OR just Docker, if you don't want to install a compiler at all

### Option A: Build and run locally

    make
    ./my_redis_server --requirepass yourpassword

Runs on 127.0.0.1:6380 by default. Omit --requirepass to run without authentication (not recommended).

You can also set the password via environment variable instead of a flag:

    REDIS_PASSWORD=yourpassword ./my_redis_server

### Option B: Run with Docker (no compiler needed)

    docker build -t my-redis-clone .
    docker run -p 6380:6380 -e REDIS_PASSWORD=yourpassword my-redis-clone

### Connect with redis-cli

    redis-cli -p 6380 -a yourpassword

### Connect from your own application code

Because this speaks the real Redis protocol, any standard Redis client library works against it unmodified.

Node.js (using ioredis):

    const Redis = require('ioredis');
    const client = new Redis(6380, 'localhost', { password: 'yourpassword' });
    await client.set('key', 'value');
    const value = await client.get('key');

Python (using redis-py):

    import redis
    r = redis.Redis(host='localhost', port=6380, password='yourpassword')
    r.set('key', 'value')
    value = r.get('key')

## Architecture

    src/
    server.cpp       - epoll event loop, connection handling
    resp.cpp         - RESP protocol parser/encoder (array + inline formats)
    storage.cpp      - in-memory data store
    commands.cpp     - command handlers
    persistence.cpp  - AOF read/write
    pubsub.cpp       - Pub/Sub channel subscription and message delivery

The server uses a single-threaded, non-blocking event loop (epoll) - the same core architecture real Redis uses - rather than spawning a thread per connection. Each client's partial/incomplete commands are buffered and reassembled correctly even when TCP delivers them in fragments. Pub/Sub messages are pushed directly to subscriber sockets in real time, independent of the publishing client's own request/response cycle.

## Benchmarks

Tested with redis-benchmark, 100,000 requests per command, running inside WSL2 (numbers are lower than native Linux due to WSL2's virtualized network layer):

    PING    ~32,700 req/sec
    SET     ~38,900 req/sec
    GET     ~33,700 req/sec
    LPUSH   ~40,200 req/sec
    RPUSH   ~41,000 req/sec
    HSET    ~40,200 req/sec

## What's Not Implemented (Yet)

- Sorted Sets
- Replication
- RDB snapshotting (only AOF persistence currently)

## License

MIT
