// Public API for per-connection HTTP handling.
//
// handle_client:
//  - Handles a single client connection: parses a simple HTTP request,
//    validates method/path, and serves a static file from docroot.
//  - Parameters:
//      client_fd : connected socket FD for the client (must be valid).
//      docroot   : path to document root directory (must remain valid for call).
//  - Behavior:
//      - Sends an HTTP response (headers + body) on client_fd.
//      - Does NOT close client_fd; caller is responsible for closing the socket.
//      - Supports GET and HEAD (note: current implementation may still send body for HEAD).
//  - Return:
//      0  on success (request handled), -1 on error (response may have been sent).
//  - Thread-safety:
//      Safe to call concurrently from multiple threads as long as docroot is immutable.
#pragma once

int handle_client(int client_fd, const char *docroot);
