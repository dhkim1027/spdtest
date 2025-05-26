# Command-Line Speedtest Application

This is a command-line application to measure network download and upload speeds using HTTP/HTTPS. It utilizes libcurl for handling HTTP/HTTPS requests and libuv for asynchronous I/O operations, allowing for multiple concurrent connections.

## Features

*   Measures download speed from a specified URL.
*   Measures upload speed to a specified URL (using generated data).
*   Supports multiple concurrent connections for both download and upload tests.
*   Configurable via command-line arguments:
    *   Test type (download/upload)
    *   Target URL
    *   Number of concurrent connections (1-10)
*   Displays results including total bytes transferred, time taken, and speed in Mbps.
*   Provides error handling for network issues and invalid inputs.

## Dependencies

To build and run this application, you need the following libraries:

*   **libcurl**: For HTTP/HTTPS operations.
*   **libuv**: For asynchronous I/O and event loop management.
*   **A C compiler** (like GCC or Clang) and `make`.

### Dependency Installation

**On Debian/Ubuntu Linux:**
```bash
sudo apt-get update
sudo apt-get install -y libcurl4-openssl-dev libuv1-dev build-essential
```

**On macOS (using Homebrew):**
```bash
brew install curl libuv pkg-config make
```
*(Note: macOS comes with libcurl, but Homebrew's version is often more up-to-date and easier to link against for development. `pkg-config` is helpful for Makefiles but not strictly required if paths are standard).*

## Compilation

A `Makefile` is provided for easy compilation. Simply navigate to the project directory in your terminal and run:

```bash
make
```
This will produce an executable named `speedtest` in the project directory.

To clean up compiled files, you can run:
```bash
make clean
```

## Usage

The application is controlled via command-line arguments.

**Syntax:**
```bash
./speedtest [options]
```

**Options:**

*   `-d`, `--download`: Perform a download speed test.
*   `-u`, `--upload`: Perform an upload speed test.
*   `-l <URL>`, `--url <URL>`: Specify the target URL for download/upload tests.
    *   If not provided, defaults to `http://speedtest.tele2.net/1MB.zip`.
*   `-c <N>`, `--connections <N>`: Specify the number of concurrent connections (integer, 1 to 10).
    *   If not provided, defaults to 1 connection.
*   `-h`, `--help`: Display the help message.

**Important:** You must specify at least one test type (`-d` or `-u`).

### Examples

1.  **Perform a download test using the default URL and 1 connection:**
    ```bash
    ./speedtest -d
    ```

2.  **Perform an upload test using the default URL and 2 connections:**
    ```bash
    ./speedtest -u -c 2
    ```

3.  **Perform a download test from a specific URL with 4 connections:**
    ```bash
    ./speedtest -d -l http://ipv4.download.thinkbroadband.com/10MB.zip -c 4
    ```
    *(Note: Use publicly available test files for accurate measurements.)*

4.  **Perform both a download and an upload test to a specific URL with 3 connections:**
    ```bash
    ./speedtest -d -u -l http://your-test-server.com/endpoint -c 3
    ```
    *(Note: For upload tests, ensure the server endpoint is configured to accept POST/PUT requests with data.)*

5.  **Display help message:**
    ```bash
    ./speedtest -h
    ```

## Output Example

```
Download Test:
Connections: 4
Total Bytes: 10485760
Time Taken: 1.23 seconds
Speed: 68.17 Mbps
```

## Error Handling

The application includes error handling for:
*   Invalid command-line arguments.
*   Inaccessible or invalid URLs.
*   Network connection failures or timeouts during tests.
Error messages are typically printed to `stderr`.
```
