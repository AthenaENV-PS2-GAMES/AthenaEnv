#ifndef ATHENA_REQUEST_H
#define ATHENA_REQUEST_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>

enum AthenaRequestMethod {
    ATHENA_REQUEST_GET = 0,
    ATHENA_REQUEST_POST,
    ATHENA_REQUEST_HEAD,
};

typedef struct AthenaRequestChunk {
    union {
        char *memory;
        FILE *fp;
    };
    size_t size;
    bool transferring;
    clock_t timer;
} AthenaRequestChunk;

typedef struct AthenaRequest {
    int tid;
    bool ready;
    bool save;
    int method;
    const char *url;
    const char *error;
    long keepalive;
    int timeout_ms;
    int verify_tls;
    int follow_redirects;
    const char *userpwd;
    const char *useragent;
    const char *postdata;
    AthenaRequestChunk chunk;
    long response_code;
    char *response_headers;
    char *headers[16];
    int headers_len;
} AthenaRequest;

typedef struct AthenaRequestResponse {
    char *body;
    size_t body_size;
    long status_code;
    char *headers;
} AthenaRequestResponse;

AthenaRequest *athena_request_create(void);
void athena_request_destroy(AthenaRequest *req);

void athena_request_set_keepalive(AthenaRequest *req, bool keepalive);
void athena_request_set_useragent(AthenaRequest *req, const char *useragent);
void athena_request_set_userpwd(AthenaRequest *req, const char *userpwd);
void athena_request_set_timeout(AthenaRequest *req, int timeout_ms);
void athena_request_set_verify_tls(AthenaRequest *req, bool verify);
void athena_request_set_follow_redirects(AthenaRequest *req, bool follow);
int athena_request_set_headers(AthenaRequest *req, const char **headers, int count);

void athena_request_run(AthenaRequest *req);
int athena_request_run_async(AthenaRequest *req, const char *task_name);
bool athena_request_poll(AthenaRequest *req, int timeout_ms, int transfer_timeout_ms);
AthenaRequestResponse *athena_request_take_response(AthenaRequest *req);
void athena_request_response_free(AthenaRequestResponse *resp);

int athena_request_get(AthenaRequest *req, const char *url, AthenaRequestResponse **out);
int athena_request_head(AthenaRequest *req, const char *url, AthenaRequestResponse **out);
int athena_request_post(AthenaRequest *req, const char *url, const char *body, AthenaRequestResponse **out);
int athena_request_download(AthenaRequest *req, const char *url, const char *path);

#endif /* ATHENA_REQUEST_H */
