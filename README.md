# TinyHTTP — a small HTTP/1.1 server in modern C++

TinyHTTP is an educational web server built from scratch with **C++17 and POSIX sockets**. It accepts TCP connections, parses a minimal HTTP request, serves files from a document root, and sends standards-shaped HTTP responses. It is designed to make operating-system and networking concepts visible rather than hide them behind a framework.

## Features

- TCP socket lifecycle: `socket → bind → listen → accept`
- Fixed-size **thread pool** with a bounded work queue (back-pressure)
- HTTP request-line and header parsing (`CRLF` framing)
- `GET` and `HEAD`; correct `Content-Length` for both
- Static-file serving with MIME type selection
- Status responses: `200`, `400`, `403`, `404`, `405`, `431`
- Partial-write-safe `send()` loop; handles `EINTR`
- Document-root containment check against `../` traversal
- Graceful `SIGINT` / `SIGTERM` shutdown and worker draining
- Request logging and configurable port, root, worker count, bind address

## Build (Linux / WSL Ubuntu)

```bash
sudo apt update
sudo apt install -y g++ cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```bash
./build/tinyhttp                 # 0.0.0.0:8080, root=www, 4 workers
./build/tinyhttp 9000 www 8      # port 9000, 8 workers
./build/tinyhttp 8080 ./www 4 127.0.0.1  # localhost only
```

## Test with curl

```bash
curl -i http://localhost:8080/
curl -i http://localhost:8080/does-not-exist   # 404
curl -I http://localhost:8080/                   # HEAD, no body
curl -i -X POST http://localhost:8080/            # 405
curl --path-as-is -i http://localhost:8080/../../etc/passwd  # blocked
```

## Architecture

```text
                 Main / accept loop
                         |
                  accepted client fd
                         |
                bounded job queue
                         |
        +----------------+----------------+
        |                |                |
     Worker 1         Worker 2        Worker N
        |                |                |
   recv request    recv request    recv request
        |                |                |
   parse HTTP      parse HTTP      parse HTTP
        |                |                |
   resolve path    resolve path    resolve path
        |                |                |
   read file       read file       read file
        |                |                |
   serialize + send response + close socket
```

## Design decisions (interview talking points)

### Why a thread pool instead of `fork()` per connection?
Creating a process for every client has relatively high overhead and duplicates process state. A fixed worker pool reuses threads and bounds concurrent work. The bounded queue provides back-pressure: when all workers and queue slots are occupied, the accept loop waits instead of allocating unbounded tasks.

### Why `send()` is called in a loop?
TCP is a byte stream. One `send()` is not guaranteed to transmit the entire response. The loop advances by the number of bytes actually written and retries if interrupted (`EINTR`).

### Why `Content-Length` matters?
HTTP/1.1 needs a way to determine where a response body ends. TinyHTTP sends the exact body byte count and `Connection: close`, making the framing unambiguous for this simple server.

### How is path traversal mitigated?
The request path is mapped under a configured document root and normalized. A component-wise containment check rejects paths that escape the root. Percent-encoded and backslash paths are rejected rather than decoded inconsistently.

### Why separate modules?
`server.cpp` owns sockets and client lifecycle; `http.cpp` owns protocol parsing/serialization; `thread_pool.cpp` owns scheduling; `main.cpp` owns configuration. Each component can be explained and tested independently.

## Current limitations / possible extensions

- One request per TCP connection (`Connection: close`); no keep-alive or pipelining
- No TLS, dynamic routes, request bodies, chunked transfer, or directory listing
- File reads are buffered in memory; large-file streaming / `sendfile()` would be a useful OS-level extension
- Minimal HTTP parser (not intended as a production Internet-facing server)

## Suggested next experiments

1. Benchmark concurrency with `wrk` or `ab` and compare 1, 2, 4, 8 workers.
2. Add `sendfile()` for zero-copy static file transfer.
3. Add timeouts and a maximum request-line length.
4. Add a small unit-test suite for parser edge cases.

## Repository layout

```text
include/       public class/interface declarations
src/           implementations and program entry point
www/            static document root
build/          generated CMake build output (not committed)
```
