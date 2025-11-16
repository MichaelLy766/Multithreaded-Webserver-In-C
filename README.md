# Multithreaded C HTTP Server (MVP)

This is a starter multithreaded HTTP server in C (MVP): acceptor + thread pool + static file serving.

Build:

    make

Run (defaults):

    ./bin/server 8080 4 ./www

Args: <port> <num_threads> <docroot>

Place static files under `www/` (create `index.html`) to test.
