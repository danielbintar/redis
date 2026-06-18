# redis-server

An in-memory key-value store inspired by Redis, implemented in C++20.

## Build

```bash
make        # build
make clean  # remove build artifacts
```

## Run

```bash
./redis-server                          # default: 127.0.0.1:6379
./redis-server --host 0.0.0.0 --port 7379
```

Connect with any Redis client:

```bash
redis-cli -p 6379 PING
redis-cli -p 6379 SET name Daniel
redis-cli -p 6379 GET name
```

## Supported commands

### Strings
`SET` `GET`

### Keys
`DEL` `EXISTS` `EXPIRE` `PEXPIRE` `TTL` `PTTL` `PERSIST` `KEYS` `DBSIZE` `FLUSHDB` `FLUSHALL`

### Server
`PING` `ECHO` `QUIT`

---

## Backlog

### String commands
- **`MSET` / `MGET`** — set/get multiple keys in one round trip.
- **`INCR` / `INCRBY` / `DECR` / `DECRBY`** — atomic integer increment/decrement; requires the stored value to be a valid integer.
- **`APPEND` / `STRLEN`** — append to an existing value; get its byte length.
- **`GETDEL` / `GETEX`** — atomic get-and-delete; get with an optional new expiry in one call.
- **`SET` options** — `NX` (only if absent), `XX` (only if present), `GET` (return old value), `KEEPTTL`, `EXAT`/`PXAT` (absolute expiry timestamp).
- **`SETNX` / `SETEX` / `GETSET`** — legacy but widely used by older clients.

### Key commands
- **`SCAN`** — cursor-based key iteration; safer than `KEYS` on large datasets because it doesn't block.
- **`KEYS` pattern matching** — currently returns all keys; needs glob support (`KEYS user:*`).
- **`TYPE`** — return the value type (`string`, `list`, `set`, …).
- **`RENAME` / `RENAMENX`** — rename a key atomically.
- **`COPY`** — copy a key to a new name without deleting the source.
- **`EXPIREAT` / `PEXPIREAT`** — set expiry to an absolute Unix timestamp instead of a relative offset.
- **`RANDOMKEY`** — return a random existing key.
- **`UNLINK`** — async delete (non-blocking alternative to `DEL`).

### Data structures
- **Sets** — `SADD`, `SREM`, `SMEMBERS`, `SISMEMBER`, `SINTER`, `SUNION`, `SDIFF`  
  Useful for unique tags, friend lists, deduplication.
- **Sorted sets** — `ZADD`, `ZRANGE`, `ZRANK`, `ZSCORE`, `ZREM`, `ZRANGEBYSCORE`  
  The most irreplaceable Redis structure — leaderboards, rate limiting, anything ranked by score.
- **Lists** — `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LLEN`  
  Task queues, activity feeds, recent history.

### Persistence
- **RDB snapshots** — periodically serialize the store to a binary file and reload on startup.
- **AOF (Append-Only File)** — write every command to a log for point-in-time recovery.

### Expiry
- **Active expiry** — background thread that periodically sweeps and evicts expired keys.  
  Currently keys are only evicted lazily on access.
- **`EXPIRETIME` / `PEXPIRETIME`** — return the absolute expiry timestamp of a key (added in Redis 7.0).

### Networking
- ~~**Thread pool / I/O multiplexing**~~ — implemented: single-threaded `kqueue` (macOS) / `epoll` (Linux) event loop with non-blocking sockets; no threads spawned per client.
- **Connection limit** — cap the maximum number of simultaneous clients to prevent resource exhaustion.
- ~~**Keepalive tuning**~~ — implemented: `SO_KEEPALIVE` enabled with 60s idle, 10s interval, 3 probes (~90s detection). `SO_RCVTIMEO` intentionally avoided — it would disconnect legitimate idle clients held by connection pools.
- **Misbehaving client timeout** — a client that connects but never sends a command holds a thread forever and won't be caught by keepalive (the client OS still responds to probes); consider a short initial-command deadline using a per-socket timer.
- **IPv6 support** — socket currently uses `AF_INET` only; extend to dual-stack `AF_INET6`.
- **Pipelining** — batch multiple commands in one TCP write and process them without waiting for individual responses.
- **Pub/Sub** — `PUBLISH`, `SUBSCRIBE`, `UNSUBSCRIBE` for real-time messaging channels.
- **RESP3** — newer protocol version with richer types (maps, sets, doubles, booleans).

### Memory management
- **`maxmemory` limit** — cap total memory use and reject writes when the limit is reached.
- **Eviction policies** — `allkeys-lru`, `allkeys-lfu`, `volatile-lru`, `volatile-ttl`, `allkeys-random` to automatically free memory under pressure.

### Configuration
- **Config file** — load settings from a `redis.conf`-style file at startup (`--config path`).
- **Daemonize** — `--daemonize` flag to fork into the background and write a PID file.
- **`COMMAND` introspection** — return metadata (arity, flags) for all supported commands.

### Replication
- **Leader/follower replication** — follower connects to a leader, receives a full snapshot, then streams incremental commands.
- **WAIT command** — block until replicas acknowledge writes.

### Transactions
- `MULTI` / `EXEC` / `DISCARD` — queue commands and execute them atomically.
- `WATCH` — optimistic locking; abort a transaction if a watched key changed.

### Observability
- `INFO` — server stats: uptime, connected clients, memory usage, hit/miss ratio.
- `MONITOR` — stream every command received by the server in real time.
- `SLOWLOG` — record commands that exceed a configurable execution time.

### Scripting
- `EVAL` — run Lua scripts server-side atomically.

### Developer tooling
- **Sanitizer build targets** — `make asan` (`-fsanitize=address,undefined`) and `make tsan` (`-fsanitize=thread`) to catch memory errors and data races during development.
- **`compile_commands.json`** — generate with `bear -- make` or `cmake` so IDEs and `clangd` get accurate autocomplete and diagnostics.
- **Fuzz testing** — fuzz the RESP parser with `libFuzzer` or AFL to find malformed-input crashes.

---

## Code quality backlog

### `server.cpp`
- **`goto disconnect`** — `handleClient` uses two `goto` jumps; replace with a helper function or a `bool should_close` flag to keep control flow structured.
- **Unsynchronised `std::cout`** — the accept loop and every client thread write to `std::cout` concurrently without a lock, producing interleaved output under load; wrap in a simple locked logger.
- **Ignored `setsockopt` return values** — calls for `SO_REUSEADDR` and `TCP_NODELAY` silently discard errors; at minimum log a warning on failure.
- **Detached threads outlive the store** — `std::thread(...).detach()` means client threads keep running after `stop()` returns and the `Store` is destroyed; track threads and join them on shutdown, or use a thread pool with a proper lifetime.

### `resp.cpp`
- **`int` for array count and bulk length** — `parseArray` stores the wire-format count/length in a plain `int`; use `int64_t` and validate against `INT_MAX` to avoid overflow on crafted input.
- **`buf.clear()` on parse error** — silently discards any pipelined commands that followed the malformed one; consider a distinct error return type so the caller can decide how to handle it.
- **Inline parser ignores quoted tokens** — `parseInline` splits on whitespace via `istringstream`, so `SET key "hello world"` inline would tokenize into four parts instead of three.

### `store.cpp`
- **Duplicated expiry check in `keys()` and `dbsize()`** — both methods manually inline the `!v.expiry || Clock::now() < *v.expiry` guard instead of reusing `getAlive()`; if the eviction logic ever changes, these will silently diverge.
- **`del()` counts already-expired keys as deleted** — it erases directly from `data_` without going through `getAlive()`, so deleting an expired-but-not-yet-evicted key returns 1 instead of 0, which doesn't match Redis semantics.

### `commands.cpp`
- **`catch (...)` is too broad** — the `SET`, `EXPIRE`, and `PEXPIRE` handlers catch all exceptions; narrow to `std::invalid_argument` and `std::out_of_range` so genuine bugs aren't silently swallowed.
- **Zero test coverage** — `commands.cpp` has no unit tests at all; add a `tests/test_commands.cpp` that exercises the dispatch layer directly (no network needed — just call `handleCommand` with a `Store`).

### `main.cpp`
- **`std::atoi` gives no error signal** — `atoi("abc")` silently returns 0, which makes the server bind to port 0 (a random OS-assigned port); replace with `std::stoi` inside a try/catch and exit with a clear message on invalid input.
- **`std::cout` inside signal handler** — `std::cout` is not async-signal-safe; use `write(STDOUT_FILENO, ...)` or set a `std::atomic<bool>` flag and let `run()` print the message.

### Tests
- **Time-dependent sleep tests** — `key_evicted_after_ttl` and `keys_excludes_expired` use `sleep_for(100ms)` with a 50 ms TTL; on a loaded CI machine the sleep may not be long enough, causing spurious failures. Use a wider margin or a poll loop with a timeout.
- **No integration test** — no test actually starts the server, connects a raw socket, sends RESP bytes, and checks the response; add at least a smoke test that covers the full request/response path.

### Makefile
- **No header dependency tracking** — `.o` files are not rebuilt when an included `.h` changes; add `-MMD -MP` to `CXXFLAGS` and `-include $(OBJS:.o=.d)` to catch header changes automatically.
- **No `make help`** — available targets are not self-documenting; add a `help` target that prints a short description of each target.
