# cpp-http-server

An HTTP/1.1 web server written from scratch in C++20. Sockets, request parsing,
routing, the thread pool and JSON encoding are all implemented in this
repository rather than delegated to a framework — the point of the project was
to understand how a web server actually works, not to assemble one.

It serves static files, exposes a small REST API, handles concurrent clients on
a fixed-size thread pool, and shuts down cleanly on Ctrl+C.

> **This is a learning and portfolio project, not a production web server.**
> It has no TLS, no chunked transfer encoding, no rate limiting and no
> authentication. Run it on localhost or a trusted LAN. See
> [Security considerations](#security-considerations).

## Features

- **TCP networking** on Winsock2 (Windows) with a small POSIX branch so the same
  code builds and is tested on Linux too
- **HTTP/1.1 parser** — methods, target, version, headers, query string, body;
  incremental, so it works with whatever `recv()` happens to return
- **Keep-alive** connections, including pipelined requests, with a
  per-connection request cap
- **Routing** with path parameters (`/api/users/{id}`), `405` + `Allow` on method
  mismatch, and a static-file fallback
- **Static file server** with MIME detection, directory index, clean URLs and
  directory-traversal protection
- **JSON REST API** (`/api/users`) with full CRUD over an in-memory store
- **Thread pool** with a *bounded* work queue — a connection flood gets `503`
  rather than unbounded memory growth
- **Structured logging** with levels and millisecond timestamps
- **Configuration file** with validation and useful error messages
- **Graceful shutdown** on Ctrl+C, bounded to roughly 250 ms
- **101 automated tests**, including integration tests that drive a real server
  over real sockets

## Architecture

```
                    main.cpp
                       │  loads Config, installs the signal handler
                       ▼
                    Server ──────────── Router ──── UserController ──── UserStore
                       │                  │
                  TcpListener             └──────── StaticFileHandler
                       │  select() + accept()
                       ▼
                  ThreadPool  (bounded queue; full ⇒ 503)
                       │
                       ▼
                 serveConnection            one worker thread per connection
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
     RequestParser  Router     HttpResponse ──► socket
```

A request travels: **socket → `RequestParser` → `Router` → handler →
`HttpResponse` → socket**. Each arrow is a plain function call on a value type,
which is what makes every stage testable without a network.

```
src/
  main.cpp                 argument parsing, config loading, signal handling
  server/
    Server.{hpp,cpp}       wiring, accept loop, shutdown sequencing
    Metrics.hpp            atomic counters shared across workers
  net/
    Socket.{hpp,cpp}       RAII socket + listener, platform differences
    Connection.{hpp,cpp}   per-connection read/parse/route/write loop
  http/
    HttpRequest.{hpp,cpp}  request model, case-insensitive headers
    HttpResponse.{hpp,cpp} response model and serialisation
    HttpParser.{hpp,cpp}   incremental HTTP/1.1 parser
    Url.{hpp,cpp}          percent-decoding, path normalisation, query strings
    StaticFiles.{hpp,cpp}  document root, MIME types, traversal checks
  routing/
    Router.{hpp,cpp}       pattern matching and dispatch
  threading/
    ThreadPool.{hpp,cpp}   fixed workers, bounded queue
  logging/Logger.{hpp,cpp} levelled, thread-safe logging
  config/Config.{hpp,cpp}  key = value configuration
  json/Json.{hpp,cpp}      JSON value type, parser and serialiser
  api/
    UserStore.{hpp,cpp}    in-memory store behind a shared_mutex
    UserController.{hpp,cpp}  /api/users handlers
    SystemController.{hpp,cpp} /api/health and /api/benchmark
tests/                     unit and integration tests
tools/BenchmarkClient.cpp  closed-loop load generator
public/                    demo site served from the document root
```

### Concurrency design

The acceptor thread owns the listening socket and does nothing but accept
connections and hand them to the pool. Each accepted connection is served
start-to-finish on one worker thread, so a single request never needs to
coordinate across threads.

| Primitive | Where | Why |
|---|---|---|
| `std::thread` | `ThreadPool` | Fixed set of workers created once; threads are far too expensive to create per connection. |
| `std::mutex` + `std::condition_variable` | `ThreadPool` | Workers sleep instead of spinning while the queue is empty; the mutex guards the queue and the stop flag together. |
| `std::atomic<bool>` | `Server::running_`, shutdown flag in `main` | Read without a lock by the accept loop and by workers between read slices. It is also the only thing a signal handler is allowed to touch. |
| `std::atomic<uint64_t>` | `Metrics` | Counters incremented from every worker; relaxed ordering is enough because nothing is ordered against them. |
| `std::shared_mutex` | `UserStore` | Reads dominate and can run concurrently; writes take the exclusive lock. |
| `std::unique_ptr` | `Server` members | The thread pool and static handler are constructed after config validation and have a clear single owner. |
| `std::shared_ptr` | accept loop | `std::function` requires a copyable callable but `TcpSocket` is deliberately move-only, so socket ownership is shared into the task. |

The accept loop polls with `select()` on a 200 ms timeout rather than blocking
in `accept()` forever. Blocking `accept()` would have to be broken by closing
the listening socket from another thread, which is racy; polling costs one
syscall every 200 ms and makes shutdown obvious.

## Requirements

- **Windows**: Visual Studio 2022 (MSVC 19.3x) or newer, CMake 3.20+
- **Linux** (also supported): GCC 13+ or Clang 16+, CMake 3.20+
- No external libraries. Nothing is downloaded at configure time.

## Build

### Windows (Visual Studio)

```bat
git clone https://github.com/<you>/cpp-http-server.git
cd cpp-http-server
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The binaries land in `build\Release\`, with `public\` and `server.conf` copied
next to them so the server runs straight from the build directory.

### Windows (command line, Ninja)

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux / macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```bat
cd build\Release
httpserver.exe
```

```
2026-01-04 10:22:31.004 [INFO] server listening on 127.0.0.1:8080 with 8 worker threads
2026-01-04 10:22:31.004 [INFO] document root: C:\dev\cpp-http-server\build\Release\public
2026-01-04 10:22:35.117 [INFO] GET /api/users 200 0.14ms 127.0.0.1:51884
```

Open <http://localhost:8080/> for the demo page, which lists and creates users
through the API. Press **Ctrl+C** to stop.

Command-line options override the config file:

```
--config <path>    configuration file (default: server.conf if present)
--port <number>    override the listen port
--root <path>      override the document root
--log-level <l>    debug | info | warning | error
--help
```

## API examples

```bash
curl http://localhost:8080/
curl http://localhost:8080/api/users
curl http://localhost:8080/api/users/1
curl "http://localhost:8080/api/users?name=Ali"

curl -X POST http://localhost:8080/api/users \
     -H "Content-Type: application/json" \
     -d '{"name":"Carol","email":"carol@example.com"}'

curl -X PUT http://localhost:8080/api/users/2 \
     -H "Content-Type: application/json" \
     -d '{"name":"Bobby","email":"bobby@example.com"}'

curl -i -X DELETE http://localhost:8080/api/users/1

curl http://localhost:8080/api/health
curl http://localhost:8080/api/benchmark
```

`GET /api/users`:

```json
{"users":[{"id":1,"name":"Alice","email":"alice@example.com"}],"count":1}
```

`POST /api/users` returns `201` with a `Location` header:

```
HTTP/1.1 201 Created
Content-Type: application/json
Location: /api/users/3
```

| Method | Path | Result |
|---|---|---|
| GET | `/` , `/about` | Static pages from the document root |
| GET | `/api/users` | `200` list, optional `?name=` substring filter |
| GET | `/api/users/{id}` | `200` or `404`; `400` if the id is not an integer |
| POST | `/api/users` | `201` + `Location`, or `400` on an invalid body |
| PUT | `/api/users/{id}` | `200` or `404` |
| DELETE | `/api/users/{id}` | `204` or `404` |
| GET | `/api/health` | `200 {"status":"ok"}` |
| GET | `/api/benchmark` | `200` counters snapshot |

Status codes the server can return: `200`, `201`, `204`, `400`, `403`, `404`,
`405`, `408`, `413`, `414`, `500`, `501`, `503`, `505`.

## Configuration

`server.conf` sits next to the binary. Every key is optional; these are the
defaults. `#` starts a comment, including at the end of a line.

```ini
bind_address = 127.0.0.1
port = 8080
document_root = ./public
worker_threads = 8          # 0 uses std::thread::hardware_concurrency()

request_timeout = 5000      # ms to wait for a complete request
keep_alive_timeout = 15000  # ms an idle keep-alive connection is held open
max_requests_per_connection = 100

max_header_size = 8192
max_body_size = 1048576
max_queued_connections = 256
listen_backlog = 128

log_level = info            # debug | info | warning | error
# log_file = server.log
```

An unknown key or an unparseable value aborts startup with the offending line
number, rather than being silently ignored.

## Testing

```bash
cmake --build build
cd build && ctest --output-on-failure
```

Or run the executables directly, optionally filtering by substring:

```bat
build\Release\unit_tests.exe
build\Release\unit_tests.exe Traversal
build\Release\integration_tests.exe
```

There are **86 unit tests** and **15 integration tests**. The integration tests
start a real `Server` on an OS-assigned port and talk to it over real sockets
with a client that does not share the server's parser, so a parser bug cannot
hide itself.

Covered: request parsing (including partial, pipelined and malformed input),
response serialisation, routing and route parameters, query parameters,
configuration parsing, JSON round-tripping, static file handling, path
traversal, the user API, thread pool behaviour under a full queue and throwing
tasks, keep-alive, timeouts, concurrent clients, and the `4xx`/`5xx` paths.

The test harness is ~80 lines in `tests/TestFramework.hpp`. Vendoring a
framework or fetching one at configure time seemed a poor trade for test
registration and two assertion macros.

### Sanitizers

On Linux the suites also run clean under ASan/UBSan and ThreadSanitizer:

```bash
cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j && ./build-tsan/integration_tests
```

## Benchmarking

`benchmark_client` is a closed-loop load generator: N keep-alive connections
each send requests back to back for a fixed duration. Closed-loop means the
reported rate is bounded by latency, which is what you want when comparing
configurations on one machine.

```bat
build\Release\httpserver.exe
build\Release\benchmark_client.exe --port 8080 --path /api/benchmark --connections 8 --seconds 10
```

```
--host <ip>          default 127.0.0.1
--port <number>      default 8080
--path <path>        default /api/benchmark
--connections <n>    default 8
--seconds <n>        default 10
```

It reports completed requests, requests/sec, and average/p50/p95/p99/max
latency. `GET /api/benchmark` is the endpoint to hammer — it does almost no work
beyond reading counters, so the measurement reflects the server rather than a
handler. Reconnects are counted separately and are expected: the server closes
a connection after `max_requests_per_connection`.

While it runs, watch `/api/benchmark` for accepted/rejected connections and
queue depth, and Task Manager (or `top`) for CPU and memory.

**No benchmark numbers are published here on purpose.** Throughput depends
entirely on the machine, and numbers measured on my hardware would tell you
nothing about yours. Run the command above and record your own. Things worth
measuring: `worker_threads` at 1 vs. core count vs. 4× core count; static files
vs. the JSON endpoint; and `--connections` past `max_queued_connections` to see
`503`s appear.

## Known limitations

- **No TLS.** Plain HTTP only.
- **No chunked transfer encoding.** A request with `Transfer-Encoding` is
  rejected with `501` rather than mis-parsed.
- **Thread-per-connection from a fixed pool.** Concurrency is capped by
  `worker_threads`; a slow client occupies a worker for the duration of its
  connection. A real high-concurrency server would use IOCP or epoll.
- **Responses are buffered fully in memory**, so files are capped at 32 MB.
  There is no `sendfile`, no streaming, no range requests.
- **The user store is in-memory.** Data is lost on restart.
- **IPv4 only.**
- **No compression, caching headers beyond `no-cache`, `ETag`, or
  conditional requests.**
- **No `OPTIONS`, `PATCH`, `TRACE`, or `CONNECT`** — they return `501`.
- **HTTP/1.0** is parsed and answered, but without `100-continue` support.

## Security considerations

Implemented defences:

- **Directory traversal** is blocked twice over: the parser removes `.`/`..`
  segments after percent-decoding (so `%2e%2e%2f` is normalised, not just
  filtered), and the static handler independently re-checks the canonical
  resolved path against the canonical document root, which also catches escapes
  through symlinks. Both layers have tests.
- **Size limits** on the header section (`413`), the request body (`413`) and
  the request target (`414`), enforced during parsing so oversized input is
  rejected before it is buffered.
- **Timeouts** on receiving a request (`408`), on idle keep-alive connections,
  and a cap on requests per connection.
- **Bounded connection queue** — when the pool's queue is full, new connections
  get `503` and are closed, so load sheds instead of exhausting memory.
- **Strict parsing.** Malformed request lines, invalid header names, obsolete
  line folding, bad `Content-Length` and invalid percent-encoding are rejected
  with `400`. Percent-decoded NUL bytes are refused outright.
- **Exception containment.** A throwing handler becomes a `500` for that one
  request; a throwing pool task is logged and the worker survives.
- **HTML escaping** in error pages, so a reflected path cannot inject markup.

Not implemented, and why it matters: TLS, authentication, authorisation, rate
limiting or per-IP connection limits, request logging redaction, and slowloris
protection beyond the per-request timeout. Do not expose this to the internet.

## Future improvements

- Replace the thread-per-connection model with IOCP on Windows / epoll on Linux
- Streaming responses and `sendfile`, with HTTP range request support
- Chunked transfer encoding, both directions
- `ETag` / `If-None-Match` and `304` handling
- A pluggable persistence layer behind `UserStore`
- Per-IP connection limits and a simple token-bucket rate limiter
- Structured (JSON) access logs with request IDs

## License

MIT. See `LICENSE`.
