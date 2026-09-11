# Architecture notes

Design decisions and their tradeoffs, in more detail than the README.

## Layering

The dependency direction is strictly one-way:

```
server ──► net ──► http ──► (nothing)
   │        │        ▲
   │        └────────┤
   ├──► routing ─────┤
   ├──► threading    │
   └──► api ─────────┘ (api also uses json)
```

`http/` knows nothing about sockets. `net/` knows nothing about routes. `api/`
knows nothing about either — a controller takes an `HttpRequest` and returns an
`HttpResponse`. That is the property the tests lean on: parsing, routing and the
API are all exercised as plain function calls, and only the integration suite
needs a socket.

## Why the parser is incremental

`recv()` returns whatever happens to have arrived, which may be half a header
line or three pipelined requests. A parser that assumes one call gives one whole
request works on localhost and fails on a real network.

`RequestParser::consume()` takes a chunk and returns `NeedMore`, `Complete` or
`Failed`. The connection loop keeps calling it until the state changes. Because
the parser holds all the state, the loop stays short, and the parser can be
tested by feeding it a request one byte at a time — which
`WaitsForIncompleteRequest` does.

`leftover()` exposes bytes that arrived after the current message so pipelined
requests are not dropped. The buffer is copied into the next parser rather than
shared; at these sizes the copy is not worth avoiding.

**Tradeoff:** the whole request, body included, is buffered in memory before
routing. That is what caps the body size at 1 MB by default. Streaming bodies to
a handler would be a more serious design and would complicate every handler
signature.

## Why the accept loop polls

Two ways to break a blocking `accept()` for shutdown:

1. Close the listening socket from another thread. `accept()` then fails — but
   whether it fails or returns a valid connection is a race, and on some
   platforms the behaviour is unspecified.
2. Connect to yourself to wake it up. Works, but it means the shutdown path
   creates a connection, which is awkward to reason about.

Instead `acceptWithTimeout()` calls `select()` with a 200 ms timeout and only
calls `accept()` when the socket is readable. Shutdown is then just an atomic
flag the loop checks, and the cost is one syscall per 200 ms while idle.

## Why reads are sliced

The first version set the socket receive timeout to the full request or
keep-alive timeout and blocked. That was correct but made Ctrl+C take up to 15
seconds: a worker parked on an idle keep-alive connection could not be joined
until its timeout expired.

The read now uses a 250 ms socket timeout and tracks the real deadline with
`steady_clock`. On each slice the worker checks whether the server is still
running, and whether the deadline has actually passed. Shutdown is bounded by
one slice plus any in-flight handler; measured at ~0.26 s.

The deadline is reset whenever bytes arrive, so the timeout applies to a
*stalled* request, not to a large one arriving steadily.

## Why the queue is bounded

An unbounded queue turns a connection flood into unbounded memory growth, and
the requests at the back of the queue will have timed out client-side by the
time a worker reaches them. Doing that work is pure waste.

With a bound, `submit()` returns `false` when full and the acceptor answers
`503` with `Retry-After` and closes. Load sheds at the front door, where it is
cheapest, and the rejection is visible in `/api/benchmark`.

**Tradeoff:** a burst shorter than the queue would have been absorbed is now
refused. `max_queued_connections` is configurable for exactly that reason.

## Ownership

`TcpSocket` is move-only and closes in its destructor. That is what makes the
error paths safe: every early return in the connection loop — timeout, malformed
request, client disconnect, shutdown — releases the handle without a single
explicit `close()` call in that function.

The one place this is awkward is `ThreadPool::submit`, which takes a
`std::function` and therefore needs a copyable callable. The socket is wrapped
in a `shared_ptr` and moved back out inside the task. A move-only task type
would avoid the allocation; it did not seem worth writing one for a single call
site.

Two lifetime bugs found during development, both worth remembering:

- `UserController` was originally a local in the `Server` constructor. Its route
  handlers are lambdas capturing `this`, so every `/api/users` request
  dereferenced a destroyed object. It is now a member declared *before* the
  router, so destruction order is right too.
- `ConnectionContext` was a local in `acceptLoop()` captured by reference into
  pool tasks. The accept thread is joined before the pool drains, so in-flight
  requests briefly held a reference to a dead frame. It is now constructed
  inside the task.

Neither showed up in normal use. The first crashed immediately under the
integration tests; the second was found by reading the shutdown ordering.

## Shutdown ordering

```
signal handler        sets an atomic flag (the only thing it may safely touch)
main loop             observes the flag, calls Server::stop()
Server::stop()        running_ = false
                      join the accept thread      → no new connections
                      pool.shutdown()             → workers drain, then join
                      close the listening socket
```

The order matters. Closing the listener first would drop connections that were
accepted but not yet served. Shutting the pool down before joining the acceptor
would let the acceptor submit to a dead pool.

Queued-but-unstarted connections *are* served rather than dropped: the worker
loop exits only when the queue is empty and `stopping_` is set. For a server
this size, finishing the handful of queued requests is better than refusing
them, and it takes milliseconds.

## Router matching

Routes are a `std::vector` scanned linearly, comparing pre-split path segments.
For a table this size that is faster than a trie and much easier to read. If the
route count grew into the hundreds, bucketing by segment count and then by first
segment would be the obvious next step.

`HEAD` is matched as `GET`; the connection layer drops the body while keeping
`Content-Length`, which is what RFC 7231 requires.

A path that matches a route's shape but not its method produces `405` with an
`Allow` header rather than `404`. Collecting the allowed methods requires
scanning all routes even after a shape match, which is why the loop continues
instead of returning early.

## JSON

Objects are a `std::vector<std::pair<std::string, Value>>`, not a map. Insertion
order is preserved, so output is stable and diffable, and lookup is linear —
irrelevant for the handful of keys an API body carries.

The parser is recursive descent with a depth cap of 64, because deeply nested
input is otherwise a stack-overflow vector. `\u` escapes are decoded to UTF-8 for
the basic multilingual plane; surrogate pairs become the replacement character,
which is a documented shortcut rather than an oversight.

## Platform initialisation

Winsock refuses every call until `WSAStartup` has run, and it is per-process,
not per-socket. The first version exposed an RAII `NetworkStack` type that each
entry point was expected to construct in `main`. That was a latent bug: the test
binary never constructed one, so on Windows every integration test failed with
"the application has not called WSAStartup" while Linux — where the type was a
no-op — passed happily.

Initialisation now lives inside the socket layer: `bindAndListen` and
`connectTcp` call `ensureNetworkReady()`, which initialises a function-local
`static` guard. Static initialisation is thread-safe, `WSACleanup` runs at
process exit, and Winsock reference-counts the calls. Anything that creates a
socket is now correct by construction, and there is no requirement for callers
to remember anything.

The general lesson: an initialisation step a caller can forget is a bug waiting
for the one caller that forgets. Pushing it behind the API that needs it removes
the category.

## Portability note: raw strings and the MSVC preprocessor

Test assertions never pass a raw string literal (`R"(...)"`) directly as a macro
argument. MSVC's legacy preprocessor mis-tokenises raw literals inside macro
arguments and reports "illegal escape sequence"; GCC and Clang accept it, so the
problem only appears on a Windows build. Where a test needs a raw literal, it is
assigned to a named `const std::string` first and the variable is passed to
`CHECK_EQ`. `/Zc:preprocessor` would also fix it, but hoisting keeps the tests
buildable on every supported toolchain without extra flags.
