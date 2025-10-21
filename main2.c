#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <uv.h>

typedef struct
{
  char *data;
  size_t size;
} http_response_t;

static size_t write_callback(void *contents, size_t size, size_t nmemb, http_response_t *response)
{
  size_t realsize = size * nmemb;
  char *ptr = realloc(response->data, response->size + realsize + 1);

  if (ptr == NULL)
  {
    printf("Not enough memory (realloc returned NULL)\n");
    return 0;
  }

  response->data = ptr;
  memcpy(&(response->data[response->size]), contents, realsize);
  response->size += realsize;
  response->data[response->size] = 0;

  return realsize;
}

void timer_callback(uv_timer_t *handle)
{
  printf("Timer callback executed\n");
}

int main()
{
  printf("spdtest - Speed Test Application\n");
  printf("Using libcurl %s and libuv %s\n", curl_version(), uv_version_string());

  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_DEFAULT);

  CURL *curl = curl_easy_init();
  if (curl)
  {
    http_response_t response = {0};

    curl_easy_setopt(curl, CURLOPT_URL, "http://httpbin.org/get");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    printf("Making HTTP request...\n");
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    else
    {
      printf("HTTP Response received (%zu bytes)\n", response.size);
    }

    if (response.data)
    {
      free(response.data);
    }

    curl_easy_cleanup(curl);
  }

  // Initialize libuv event loop
  uv_loop_t *loop = uv_default_loop();

  uv_timer_t timer;
  uv_timer_init(loop, &timer);

  printf("Starting libuv timer for 2 seconds...\n");
  uv_timer_start(&timer, timer_callback, 2000, 0);

  printf("Running event loop...\n");
  uv_run(loop, UV_RUN_DEFAULT);

  printf("Cleaning up...\n");
  uv_loop_close(loop);
  curl_global_cleanup();

  printf("Application finished successfully\n");
  return 0;
}