# Multithreaded C HTTP Server

This is a multithreaded HTTP server in C: acceptor + thread pool + static file serving.
It includes a scheduler abstraction (FIFO / SJF), a simple metrics logger, and a wrk mix script for testing.

Build

    make

Run (defaults)

    ./bin/server 8080 4 ./www

Args: `<port> <num_threads> <docroot>`

Scheduler selection

- CLI flag: `--scheduler=fifo` or `--scheduler=sjf`
  
  ```
  ./bin/server 8080 4 ./www --scheduler=fifo
  ```
  
- Or environment variable `SCHEDULER`:
  
  ```
  SCHEDULER=fifo ./bin/server 8080 4 ./www
  ```
  
Default scheduler is `sjf` (unless overridden).

Metrics & logging

- A lightweight metrics thread prints aggregates every 5s to stderr:
  - req/s, MB/s, avg latency, total requests, submit est==0 fraction, etc.
- The server prints submit logs like `submit: fd=... est=...` to help validate SJF estimates.
- Run server in foreground to see logs, or redirect to a file:
  
  ```
  ./bin/server 8080 4 ./www > server.log 2>&1
  tail -f server.log
  ```

Testing with wrk

- Add test files under `www/`:
  
  ```
  mkdir -p www
  echo '<html>small</html>' > www/small.txt
  dd if=/dev/zero of=www/big.bin bs=1M count=1
  ```
  
- A sample wrk mix script is provided at `scripts/mix.lua`. Run:
  
  ```
  wrk -t4 -c200 -d30s -s scripts/mix.lua http://localhost:8080
  ```
  
- Or test individual endpoints:
  
  ```
  wrk -t4 -c200 -d30s http://localhost:8080/small.txt
  wrk -t4 -c200 -d30s http://localhost:8080/big.bin
  ```

Notes

- SJF uses a best-effort `est_cost` (file size) obtained via a `recv(MSG_PEEK)` + `stat()` during accept. If the acceptor can't estimate, `est_cost` may be 0.