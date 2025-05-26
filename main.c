#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <curl/curl.h>
#include <uv.h>

// Global variables for libuv and libcurl integration
uv_loop_t *loop;
CURLM *curl_multi_handle;
uv_timer_t timeout_timer; // For libcurl's internal timing
static int running_handles = 0; // Active CURL easy handles
static long long total_downloaded_bytes = 0;

// Forward declarations
static void check_multi_info(void);
static void perform_download_test(const char *url, int num_connections);
static void perform_upload_test(const char *url, int num_connections);
static void print_test_results(const char* test_type, int connections, long long total_bytes, double time_taken_s, double speed_mbps); // Added

// --- Upload specific structures ---
typedef struct {
    char *buffer;
    size_t size;
} upload_buffer_info_t;

typedef struct {
    CURL *easy_handle;
    upload_buffer_info_t *buffer_info; // Pointer to the shared buffer
    size_t bytes_sent;
    // char unique_id[16]; // For debugging if needed
} upload_stream_context_t;
// --- End Upload specific structures ---

typedef struct {
    CURL *easy_handle;
    // Potentially other per-stream data later
} download_context_t;

struct arguments {
    int download_test;
    int upload_test;
    char *url;
    int connections;
    int help_flag;
};

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d, --download         Perform a download speed test.\n");
    printf("  -u, --upload           Perform an upload speed test.\n");
    printf("  -l, --url <URL>        Specify the target URL for tests.\n");
    printf("                         (Default: http://speedtest.tele2.net/1MB.zip)\n");
    printf("  -c, --connections <N>  Specify the number of concurrent connections (1-10).\n");
    printf("                         (Default: 1)\n");
    printf("  -h, --help             Display this help message.\n");
}

int main(int argc, char *argv[]) {
    struct arguments arguments;
    // Default values
    arguments.download_test = 0;
    arguments.upload_test = 0;
    arguments.url = "http://speedtest.tele2.net/1MB.zip";
    arguments.connections = 1;
    arguments.help_flag = 0;

    static struct option long_options[] = {
        {"download", no_argument, 0, 'd'},
        {"upload", no_argument, 0, 'u'},
        {"url", required_argument, 0, 'l'},
        {"connections", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0} // Terminator
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "dul:c:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                arguments.download_test = 1;
                break;
            case 'u':
                arguments.upload_test = 1;
                break;
            case 'l':
                arguments.url = optarg;
                break;
            case 'c':
                arguments.connections = atoi(optarg);
                if (arguments.connections < 1 || arguments.connections > 10) {
                    fprintf(stderr, "Error: Number of connections must be between 1 and 10.\n");
                    print_usage(argv[0]);
                    return 1;
                }
                break;
            case 'h':
                arguments.help_flag = 1;
                break;
            default: /* '?' */
                print_usage(argv[0]);
                return 1;
        }
    }

    if (arguments.help_flag) {
        print_usage(argv[0]);
        return 0;
    }

    if (!arguments.download_test && !arguments.upload_test) {
        fprintf(stderr, "Error: At least one test type (-d or -u) must be specified.\n");
        print_usage(argv[0]);
        return 1;
    }

    // URL Argument Check
    if (arguments.url == NULL || strlen(arguments.url) == 0) {
        fprintf(stderr, "Error: Target URL is missing or empty. Please specify a URL with -l or --url.\n");
        print_usage(argv[0]);
        return 1;
    }

    printf("Speedtest application starting...\n");
    printf("Configuration:\n");
    if (arguments.download_test) {
        printf("  - Download test enabled\n");
    }
    if (arguments.upload_test) {
        printf("  - Upload test enabled\n");
    }
    printf("  - URL: %s\n", arguments.url);
    printf("  - Connections: %d\n", arguments.connections);

    // Initialize libuv and libcurl
    loop = uv_default_loop();
    if (!loop) {
        fprintf(stderr, "Failed to initialize libuv loop.\n");
        return 1;
    }

    CURLcode global_init_rc = curl_global_init(CURL_GLOBAL_ALL);
    if (global_init_rc != CURLE_OK) {
        fprintf(stderr, "Error: Failed to initialize libcurl global state: %s\n", curl_easy_strerror(global_init_rc));
        // uv_loop_close(loop); // Loop not used yet for anything complex, direct exit is fine.
        return 1;
    }

    curl_multi_handle = curl_multi_init();
    if (!curl_multi_handle) {
        fprintf(stderr, "Error: Failed to initialize libcurl multi handle.\n");
        curl_global_cleanup();
        // uv_loop_close(loop);
        return 1;
    }

    int timer_init_rc = uv_timer_init(loop, &timeout_timer);
    if (timer_init_rc != 0) {
        fprintf(stderr, "Error: Failed to initialize libuv timer (timeout_timer): %s\n", uv_strerror(timer_init_rc));
        curl_multi_cleanup(curl_multi_handle);
        curl_global_cleanup();
        // uv_loop_close(loop);
        return 1;
    }

    // Set libcurl multi options for libuv integration
    curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, curl_perform_socket_action);
    curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETDATA, NULL);
    curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, handle_curl_timeout);
    curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERDATA, NULL);

    printf("libcurl and libuv initialized.\n");
    
    if (arguments.download_test) {
        perform_download_test(arguments.url, arguments.connections);
    }
    if (arguments.upload_test) {
        // For upload, typically a different URL or a URL that accepts POST/PUT is needed.
        // Using the same URL might not be representative for a real upload test.
        // For this example, we'll use it, but in a real scenario, args.url might need
        // to be different for upload, or a specific upload URL should be configurable.
        printf("\nNote: Ensure the URL '%s' is configured to accept uploads for a meaningful test.\n", arguments.url);
        perform_upload_test(arguments.url, arguments.connections);
    }

    // Cleanup is done after tests complete
    printf("Cleaning up libcurl and libuv global resources...\n");
    curl_multi_cleanup(curl_multi_handle); // Cleans up all easy handles associated with it too if not removed.
                                         // However, we explicitly clean easy handles in perform_download_test.
    curl_global_cleanup();
    
    // Ensure all libuv handles initiated by main are closed before closing the loop.
    // timeout_timer is libcurl's, it should be stopped if running_handles is 0.
    // test_duration_timer is local to perform_download_test and closed there.
    // Poll handles are managed by curl_perform_socket_action.
    
    // Stop the main libcurl timer if it's still somehow active and not cleaned by check_multi_info
    if (uv_is_active((uv_handle_t*)&timeout_timer)) {
        uv_timer_stop(&timeout_timer);
    }
    uv_close((uv_handle_t*)&timeout_timer, NULL); // Close it properly

    // Run loop to allow any pending close callbacks to execute
    uv_run(loop, UV_RUN_NOWAIT); 
    
    int loop_close_err = uv_loop_close(loop);
    if (loop_close_err == UV_EBUSY) {
        // This might happen if some handles (e.g. from curl_perform_socket_action) weren't fully cleaned up by libcurl
        fprintf(stderr, "Warning: Not all libuv handles were closed initially. Trying one more run for cleanup.\n");
        uv_run(loop, UV_RUN_ONCE); // Try to process pending close callbacks
        loop_close_err = uv_loop_close(loop);
         if (loop_close_err != 0) {
            fprintf(stderr, "Failed to close libuv loop gracefully: %s. Some handles might still be active.\n", uv_strerror(loop_close_err));
        }
    }
    // uv_default_loop() does not need to be freed by free(). uv_loop_close() handles its resources.
    printf("Application finished.\n");
    return 0;
}


// Dummy callback for the test duration timer.
// Its main purpose is to ensure uv_run doesn't exit prematurely if there are no other
// active I/O events but the test is still logically "running" based on time.
// Actual timing is done using uv_hrtime().
static void on_test_timeout_dummy(uv_timer_t *timer) {
    // printf("on_test_timeout_dummy tick (keeps event loop alive if no other events)\n");
}

static void perform_download_test(const char *url, int num_connections) {
    printf("\nStarting download test: %d connection(s) to %s\n", num_connections, url);

    total_downloaded_bytes = 0; // Reset global counter for the test

    // test_duration_timer is used to keep the event loop alive for the duration of the test,
    // independently of curl activity. We measure time using uv_hrtime.
    static uv_timer_t test_duration_timer; 
    static uint64_t test_start_time_ns;

    // Store easy handles for cleanup. Max connections is small (10).
    CURL *easy_handles[10]; // Assuming max 10 connections as per arg validation
    if (num_connections > 10) {
      fprintf(stderr, "Error: Exceeded maximum allowed connections for easy_handles array.\n");
      return;
    }
    int successfully_added_handles = 0;

    // Initialize and start the dummy timer.
    int timer_init_rc_dl = uv_timer_init(loop, &test_duration_timer);
    if (timer_init_rc_dl != 0) {
        fprintf(stderr, "Error: Failed to initialize download test_duration_timer: %s\n", uv_strerror(timer_init_rc_dl));
        // No handles added yet, so just return.
        return;
    }
    uv_timer_start(&test_duration_timer, on_test_timeout_dummy, 10000, 10000); 
    
    test_start_time_ns = uv_hrtime();
    CURLcode res;

    for (int i = 0; i < num_connections; ++i) {
        CURL *curl_easy = curl_easy_init();
        if (!curl_easy) {
            fprintf(stderr, "Error: curl_easy_init failed for download connection %d. Skipping.\n", i + 1);
            continue; // Skip this handle
        }

        res = curl_easy_setopt(curl_easy, CURLOPT_URL, url);
        if (res != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_URL failed for download connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res));
            curl_easy_cleanup(curl_easy);
            continue;
        }
        res = curl_easy_setopt(curl_easy, CURLOPT_WRITEFUNCTION, curl_write_callback);
        if (res != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_WRITEFUNCTION failed for download connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res));
            curl_easy_cleanup(curl_easy);
            continue;
        }
        // Non-critical options, less verbose error handling
        curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, NULL); 
        curl_easy_setopt(curl_easy, CURLOPT_PRIVATE, "download_handle"); 
        curl_easy_setopt(curl_easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_easy, CURLOPT_TIMEOUT, 60L); 
        curl_easy_setopt(curl_easy, CURLOPT_VERBOSE, 0L); 

        CURLMcode mc = curl_multi_add_handle(curl_multi_handle, curl_easy);
        if (mc == CURLM_OK) {
            easy_handles[successfully_added_handles++] = curl_easy;
        } else {
            fprintf(stderr, "Error: curl_multi_add_handle failed for download connection %d: %s. Cleaning up handle.\n", i + 1, curl_multi_strerror(mc));
            curl_easy_cleanup(curl_easy);
        }
    }
    if (successfully_added_handles == 0) {
        fprintf(stderr, "No connections were successfully initiated. Aborting download test.\n");
        if (uv_is_active((uv_handle_t*)&test_duration_timer)) {
             uv_timer_stop(&test_duration_timer);
        }
        uv_close((uv_handle_t*)&test_duration_timer, NULL); 
        uv_run(loop, UV_RUN_NOWAIT); // Allow closing callbacks for timer
        return;
    }
    
    // Set global count of active CURL transfers.
    // This is decremented in check_multi_info when a transfer completes.
    running_handles = successfully_added_handles; 
    printf("%d CURL handles added to multi_handle. Starting event loop for download...\n", running_handles);

    // uv_run will block here until:
    // 1. All CURL easy handles are removed from the multi_handle (running_handles becomes 0).
    // 2. libcurl's internal timer (timeout_timer) is stopped (done in check_multi_info when running_handles is 0).
    // 3. The test_duration_timer is stopped (we do this explicitly after uv_run returns).
    // OR other critical errors occur.
    uv_run(loop, UV_RUN_DEFAULT);
    printf("Event loop finished for download test.\n");

    uint64_t test_end_time_ns = uv_hrtime();
    double actual_test_duration_s = (test_end_time_ns - test_start_time_ns) / 1e9;

    // Stop and close the test_duration_timer (dummy timer)
    if (uv_is_active((uv_handle_t*)&test_duration_timer)) {
        uv_timer_stop(&test_duration_timer);
    }
    uv_close((uv_handle_t*)&test_duration_timer, NULL); 
    // Run the loop once more to allow close callbacks (like for test_duration_timer) to process.
    uv_run(loop, UV_RUN_NOWAIT); 

    double speed_mbps_download = 0.0;
    if (actual_test_duration_s > 0.001 && total_downloaded_bytes > 0) {
        speed_mbps_download = (total_downloaded_bytes * 8.0) / actual_test_duration_s / (1000.0 * 1000.0);
    }
    print_test_results("Download", successfully_added_handles, total_downloaded_bytes, actual_test_duration_s, speed_mbps_download);
    
    // Cleanup CURL easy handles
    printf("Cleaning up %d CURL easy handles used in the test...\n", successfully_added_handles);
    for (int i = 0; i < successfully_added_handles; ++i) {
        // Note: curl_multi_remove_handle was already called in check_multi_info
        curl_easy_cleanup(easy_handles[i]);
    }
    
    // Reset running_handles, though it should be 0 if uv_run exited cleanly after all transfers.
    running_handles = 0; 
}

// --- Upload specific helper functions ---
static void generate_upload_data(upload_buffer_info_t *buffer_info, size_t size_bytes) {
    if (!buffer_info) return;
    buffer_info->buffer = malloc(size_bytes);
    if (buffer_info->buffer) {
        // Fill with some pattern, e.g., zeros or a repeating sequence
        memset(buffer_info->buffer, 0, size_bytes); 
        // for(size_t i=0; i < size_bytes; ++i) buffer_info->buffer[i] = (char)(i % 256);
        buffer_info->size = size_bytes;
        printf("Generated %zu bytes of upload data.\n", size_bytes);
    } else {
        buffer_info->size = 0;
        fprintf(stderr, "Failed to allocate memory for upload data buffer.\n");
    }
}

static void free_upload_data(upload_buffer_info_t *buffer_info) {
    if (!buffer_info) return;
    if (buffer_info->buffer) {
        free(buffer_info->buffer);
        buffer_info->buffer = NULL;
    }
    buffer_info->size = 0;
    printf("Freed upload data buffer.\n");
}

// Libcurl read callback function for uploads
static size_t curl_read_callback(char *dest_buffer, size_t size, size_t nitems, void *userp) {
    upload_stream_context_t *stream_ctx = (upload_stream_context_t *)userp;
    if (!stream_ctx || !stream_ctx->buffer_info || !stream_ctx->buffer_info->buffer) {
        fprintf(stderr, "Read callback error: Invalid stream context or buffer.\n");
        return CURL_READFUNC_ABORT; // Abort the transfer
    }

    size_t buffer_max_provide = size * nitems;
    size_t remaining_in_stream = stream_ctx->buffer_info->size - stream_ctx->bytes_sent;
    size_t to_copy = (buffer_max_provide < remaining_in_stream) ? buffer_max_provide : remaining_in_stream;

    if (to_copy > 0) {
        memcpy(dest_buffer, stream_ctx->buffer_info->buffer + stream_ctx->bytes_sent, to_copy);
        stream_ctx->bytes_sent += to_copy;
        // printf("Read callback: provided %zu bytes for handle %p, total sent by this stream: %zu\n", 
        //        to_copy, (void*)stream_ctx->easy_handle, stream_ctx->bytes_sent);
    } else {
        // printf("Read callback: no more data to send for handle %p (total sent: %zu)\n", 
        //        (void*)stream_ctx->easy_handle, stream_ctx->bytes_sent);
    }
    return to_copy; // Return number of bytes copied
}
// --- End Upload specific helper functions ---

// --- Upload Test Implementation ---
static void perform_upload_test(const char *url, int num_connections) {
    printf("\nStarting upload test: %d connection(s) to %s\n", num_connections, url);

    static long long total_uploaded_bytes_test_run = 0; // Accumulator for this specific test run
    static uv_timer_t test_duration_timer_upload; 
    static uint64_t test_start_time_ns_upload; // Use different static for upload if needed

    upload_buffer_info_t shared_upload_data;
    generate_upload_data(&shared_upload_data, 10 * 1024 * 1024); // 10MB of data

    if (shared_upload_data.buffer == NULL || shared_upload_data.size == 0) {
        fprintf(stderr, "Upload test aborted: Failed to generate upload data.\n");
        return;
    }

    // Array to store individual stream contexts and their easy handles
    upload_stream_context_t stream_contexts[10]; // Max 10 connections
     if (num_connections > 10) {
      fprintf(stderr, "Error: Exceeded maximum allowed connections for stream_contexts array.\n");
      free_upload_data(&shared_upload_data);
      return;
    }
    int successfully_added_handles = 0;

    int timer_init_rc_ul = uv_timer_init(loop, &test_duration_timer_upload);
    if (timer_init_rc_ul != 0) {
        fprintf(stderr, "Error: Failed to initialize upload test_duration_timer: %s\n", uv_strerror(timer_init_rc_ul));
        free_upload_data(&shared_upload_data);
        return;
    }
    uv_timer_start(&test_duration_timer_upload, on_test_timeout_dummy, 1, 0); 
    
    test_start_time_ns_upload = uv_hrtime();
    total_uploaded_bytes_test_run = 0; // Reset for this run
    CURLcode res_ul; // Renamed to avoid conflict with download test's 'res' if they were in same scope

    for (int i = 0; i < num_connections; ++i) {
        stream_contexts[i].buffer_info = &shared_upload_data;
        stream_contexts[i].bytes_sent = 0;
        
        stream_contexts[i].easy_handle = curl_easy_init();
        if (!stream_contexts[i].easy_handle) {
            fprintf(stderr, "Error: curl_easy_init failed for upload connection %d. Skipping.\n", i + 1);
            continue; 
        }

        res_ul = curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_URL, url);
        if (res_ul != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_URL failed for upload connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res_ul));
            curl_easy_cleanup(stream_contexts[i].easy_handle);
            stream_contexts[i].easy_handle = NULL;
            continue;
        }
        res_ul = curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_UPLOAD, 1L);
        if (res_ul != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_UPLOAD failed for upload connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res_ul));
            curl_easy_cleanup(stream_contexts[i].easy_handle);
            stream_contexts[i].easy_handle = NULL;
            continue;
        }
        res_ul = curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_READFUNCTION, curl_read_callback);
        if (res_ul != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_READFUNCTION failed for upload connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res_ul));
            curl_easy_cleanup(stream_contexts[i].easy_handle);
            stream_contexts[i].easy_handle = NULL;
            continue;
        }
        res_ul = curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_READDATA, &stream_contexts[i]);
        if (res_ul != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_READDATA failed for upload connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res_ul));
            curl_easy_cleanup(stream_contexts[i].easy_handle);
            stream_contexts[i].easy_handle = NULL;
            continue;
        }
        res_ul = curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)shared_upload_data.size);
        if (res_ul != CURLE_OK) {
            fprintf(stderr, "Error: curl_easy_setopt CURLOPT_INFILESIZE_LARGE failed for upload connection %d: %s. Skipping.\n", i + 1, curl_easy_strerror(res_ul));
            curl_easy_cleanup(stream_contexts[i].easy_handle);
            stream_contexts[i].easy_handle = NULL;
            continue;
        }
        
        // Non-critical options
        curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_TIMEOUT, 120L); 
        curl_easy_setopt(stream_contexts[i].easy_handle, CURLOPT_VERBOSE, 0L); 

        CURLMcode mc = curl_multi_add_handle(curl_multi_handle, stream_contexts[i].easy_handle);
        if (mc == CURLM_OK) {
            successfully_added_handles++;
        } else {
            fprintf(stderr, "Error: curl_multi_add_handle failed for upload connection %d: %s. Cleaning up handle.\n", i + 1, curl_multi_strerror(mc));
            curl_easy_cleanup(stream_contexts[i].easy_handle);
            stream_contexts[i].easy_handle = NULL; // Mark as unusable
        }
    }
    if (successfully_added_handles == 0) {
        fprintf(stderr, "No upload connections were successfully initiated. Aborting upload test.\n");
        if (uv_is_active((uv_handle_t*)&test_duration_timer_upload)) {
             uv_timer_stop(&test_duration_timer_upload);
        }
        uv_close((uv_handle_t*)&test_duration_timer_upload, NULL);
        uv_run(loop, UV_RUN_NOWAIT);
        free_upload_data(&shared_upload_data);
        return;
    }

    running_handles = successfully_added_handles;
    printf("%d CURL handles added for upload. Starting event loop for upload...\n", running_handles);

    uv_run(loop, UV_RUN_DEFAULT); // Loop runs as long as there are active handles (curl requests, timers)
    printf("Event loop finished for upload test.\n");

    uint64_t test_end_time_ns_upload = uv_hrtime();
    double actual_test_duration_s = (test_end_time_ns_upload - test_start_time_ns_upload) / 1e9;

    if (uv_is_active((uv_handle_t*)&test_duration_timer_upload)) {
        uv_timer_stop(&test_duration_timer_upload);
    }
    uv_close((uv_handle_t*)&test_duration_timer_upload, NULL);
    uv_run(loop, UV_RUN_NOWAIT); // Allow timer close callback to run

    // Calculate total uploaded bytes by summing from contexts
    long long current_total_uploaded_bytes = 0; // Use a local variable for this calculation
    for (int i = 0; i < num_connections; ++i) {
        if (stream_contexts[i].easy_handle) { // Only count if handle was successfully used
             current_total_uploaded_bytes += stream_contexts[i].bytes_sent;
        }
    }
    // Assign to the static variable if you intend to use it elsewhere, or just use the local one for printing.
    total_uploaded_bytes_test_run = current_total_uploaded_bytes;


    double speed_mbps_upload = 0.0;
    if (actual_test_duration_s > 0.001 && total_uploaded_bytes_test_run > 0) {
        speed_mbps_upload = (total_uploaded_bytes_test_run * 8.0) / actual_test_duration_s / (1000.0 * 1000.0);
    }
    print_test_results("Upload", successfully_added_handles, total_uploaded_bytes_test_run, actual_test_duration_s, speed_mbps_upload);

    // Cleanup CURL easy handles
    printf("Cleaning up %d CURL easy handles used in the upload test...\n", successfully_added_handles);
    for (int i = 0; i < num_connections; ++i) {
        if (stream_contexts[i].easy_handle) {
            // curl_multi_remove_handle is called by check_multi_info when CURLMSG_DONE
            curl_easy_cleanup(stream_contexts[i].easy_handle);
        }
    }
    free_upload_data(&shared_upload_data);
    running_handles = 0; // Reset
}
// --- End Upload Test Implementation ---

// --- Results Printing Function ---
static void print_test_results(const char* test_type, int connections, long long total_bytes, double time_taken_s, double speed_mbps) {
    printf("\n--- %s Test Results ---\n", test_type);
    // Note: The original format had "Target URL" and "Requested Connections" which are not parameters here.
    // If those are strictly needed, this function signature or its usage would need adjustment.
    // For now, sticking to the provided signature.
    printf("Connections: %d\n", connections); // This refers to successfully initiated connections
    printf("Total Bytes: %lld\n", total_bytes);
    printf("Time Taken: %.2f seconds\n", time_taken_s);
    if (speed_mbps > 0.0) {
        printf("Speed: %.2f Mbps\n", speed_mbps);
    } else if (total_bytes > 0 && time_taken_s <= 0.001) {
        printf("Speed: N/A (duration too short for reliable calculation, but data was transferred)\n");
    } else if (total_bytes == 0 && time_taken_s > 0.001) {
        printf("Speed: 0.00 Mbps (no data transferred)\n");
    }
    else {
        printf("Speed: N/A (no data transferred or duration too short)\n");
    }
    printf("---------------------------\n\n");
}
// --- End Results Printing Function ---

// Function to free uv_poll_t handles
static void free_poll_handle(uv_handle_t *handle) {
    free(handle);
}

// Called by libuv when the curl timer expires
static void on_uv_curl_timeout(uv_timer_t *timer) {
    // printf("on_uv_curl_timeout called\n");
    int local_still_running = 0; 
    CURLMcode mc = curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &local_still_running);
    if (mc != CURLM_OK) {
        fprintf(stderr, "curl_multi_socket_action (timeout) failed: %s\n", curl_multi_strerror(mc));
    }
    running_handles = local_still_running; // Update global based on libcurl's report
    check_multi_info(); 
    // Note: The decision to stop the loop or specific timers is complex.
    // check_multi_info will handle stopping timeout_timer if running_handles hits 0.
    // The main event loop (uv_run in perform_download_test) will stop when all handles 
    // (including active CURL requests and potentially test_duration_timer) are inactive.
}

// Called by libcurl when it wants to set/clear a timer
static int handle_curl_timeout(CURLM *multi, long timeout_ms, void *userp) {
    // printf("handle_curl_timeout called, timeout_ms: %ld\n", timeout_ms);
    if (timeout_ms < 0) { // libcurl wants to clear the timer
        uv_timer_stop(&timeout_timer);
    } else {
        if (timeout_ms == 0) { // libcurl wants to act immediately
            int local_still_running = 0;
            CURLMcode mc = curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &local_still_running);
            if (mc != CURLM_OK) {
                fprintf(stderr, "curl_multi_socket_action (immediate timeout) failed: %s\n", curl_multi_strerror(mc));
            }
            running_handles = local_still_running; // Update global
            check_multi_info(); // Check if this immediate action completed something
        } else { // Start the timer with the specified timeout
            uv_timer_start(&timeout_timer, on_uv_curl_timeout, timeout_ms, 0);
        }
    }
    return 0;
}

// Called by libuv when there's an event on a socket monitored for libcurl
static void on_uv_socket_event(uv_poll_t *handle, int status, int events) {
    // printf("on_uv_socket_event called, status: %d, events: %d\n", status, events);
    if (status < 0) { // Error status from libuv
        fprintf(stderr, "on_uv_socket_event error: %s\n", uv_strerror(status));
        // Potentially close or remove the handle here, but curl_multi_socket_action should handle it
    }

    int flags = 0;
    if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
    if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;

    uv_os_fd_t sockfd;
    if (uv_fileno((uv_handle_t*)handle, &sockfd) != 0) {
        fprintf(stderr, "Failed to get socket fd from uv_poll_t handle.\n");
        return;
    }
    
    int local_still_running = 0;
    CURLMcode mc = curl_multi_socket_action(curl_multi_handle, sockfd, flags, &local_still_running);
    if (mc != CURLM_OK) {
        fprintf(stderr, "curl_multi_socket_action (socket event) failed: %s\n", curl_multi_strerror(mc));
    }
    running_handles = local_still_running; // Update global
    check_multi_info();
}

// Libcurl write callback function
static size_t curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    // (void)userdata; // Not used for now, but could point to download_context_t
    size_t received_bytes = size * nmemb;
    total_downloaded_bytes += received_bytes;
    // printf("Received %zu bytes, total %lld bytes\n", received_bytes, total_downloaded_bytes);
    return received_bytes; // Indicate all data was handled
}

// Check for completed CURL transfers
static void check_multi_info(void) {
    CURLMsg *msg;
    int msgs_left;
    int current_running_handles = running_handles; // Use a local copy for this iteration

    while ((msg = curl_multi_info_read(curl_multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy_handle = msg->easy_handle;
            CURLcode result = msg->data.result;
            // char *private_data = NULL;
            // curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &private_data);
            // printf("Transfer for %s completed with status %d (%s)\n", 
            //        private_data ? private_data : "unknown handle", result, curl_easy_strerror(result));
            if (result != CURLE_OK) {
                char *effective_url = NULL;
                curl_easy_getinfo(easy_handle, CURLINFO_EFFECTIVE_URL, &effective_url);
                fprintf(stderr, "Error: Transfer for URL %s failed: %s\n",
                        effective_url ? effective_url : "[unknown URL]",
                        curl_easy_strerror(result));
            } else {
                // Optionally, print success for verbosity, but problem statement implies only error reporting change
                // printf("Transfer for handle %p completed successfully.\n", (void*)easy_handle);
            }
            
            // printf("Transfer completed with status %d\n", result); // Old message

            curl_multi_remove_handle(curl_multi_handle, easy_handle);
            // DO NOT cleanup easy_handle here. It's managed by perform_download_test's list.
            
            current_running_handles--; // Decrement local counter
        }
    }
    
    running_handles = current_running_handles; // Update global running_handles
    // printf("check_multi_info: running_handles is now %d\n", running_handles);

    if (running_handles == 0 && uv_is_active((uv_handle_t*)&timeout_timer)) {
        // printf("All transfers complete, stopping libcurl's timeout_timer.\n");
        uv_timer_stop(&timeout_timer);
    }
}


// Called by libcurl when it needs to perform an action on a socket
static int curl_perform_socket_action(CURL *easy, curl_socket_t sockfd, int action, void *userp, void *socketp) {
    // printf("curl_perform_socket_action called, sockfd: %d, action: %d\n", sockfd, action);
    uv_poll_t *poll_handle = (uv_poll_t*)socketp;

    if (action == CURL_POLL_REMOVE) {
        if (poll_handle) {
            uv_poll_stop(poll_handle);
            uv_close((uv_handle_t*)poll_handle, free_poll_handle);
            curl_multi_assign(curl_multi_handle, sockfd, NULL); // Clear the socket pointer in libcurl
        }
    } else {
        if (!poll_handle) { // New socket, create and initialize uv_poll_t
            poll_handle = malloc(sizeof(uv_poll_t));
            if (!poll_handle) {
                fprintf(stderr, "Error: Failed to allocate memory for uv_poll_t in curl_perform_socket_action.\n");
                return -1; // CURL_SOCKET_BAD equivalent for error
            }
            // poll_handle->data = NULL; // Initialize if necessary, not strictly needed for current use

            // Check if loop is valid before using it (already done by program structure, but defensive)
            if (!loop) {
                 fprintf(stderr, "Error: Libuv loop not initialized in curl_perform_socket_action.\n");
                 free(poll_handle);
                 return -1; // CURL_SOCKET_BAD equivalent
            }
            int init_err = uv_poll_init_socket(loop, poll_handle, sockfd);
            if (init_err != 0) {
                fprintf(stderr, "Error: uv_poll_init_socket failed in curl_perform_socket_action: %s\n", uv_strerror(init_err));
                free(poll_handle);
                return -1; // CURL_SOCKET_BAD equivalent
            }
            // Store the poll_handle with libcurl for this socket
            CURLMcode mc = curl_multi_assign(curl_multi_handle, sockfd, poll_handle);
            if (mc != CURLM_OK) {
                fprintf(stderr, "curl_multi_assign failed: %s\n", curl_multi_strerror(mc));
                uv_close((uv_handle_t*)poll_handle, free_poll_handle); // clean up allocated handle
                return CURL_SOCKET_BAD;
            }
        }

        int events = 0;
        if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) {
            events |= UV_READABLE;
        }
        if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {
            events |= UV_WRITABLE;
        }

        if (events != 0) {
            int start_err = uv_poll_start(poll_handle, events, on_uv_socket_event);
            if (start_err != 0) {
                fprintf(stderr, "uv_poll_start failed: %s\n", uv_strerror(start_err));
                // We might not want to return CURL_SOCKET_BAD here if the handle was previously working
                // but failed to restart with new events. Libcurl might retry.
                // However, if it's a new handle and start fails, it's more critical.
                if (!socketp) { // If it was a new handle
                     uv_close((uv_handle_t*)poll_handle, free_poll_handle);
                     curl_multi_assign(curl_multi_handle, sockfd, NULL);
                     return CURL_SOCKET_BAD;
                }
            }
        } else {
            // If events is 0, it implies libcurl wants to stop monitoring this socket for now,
            // but not remove it. We can stop polling.
            uv_poll_stop(poll_handle);
        }
    }
    return CURL_SOCKET_OK; // Or appropriate error code
}
