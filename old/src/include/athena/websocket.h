#ifndef ATHENA_WEBSOCKET_H
#define ATHENA_WEBSOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <ath_net.h>

typedef struct AthenaWebSocket {
    ath_ws_ctx_t *ctx;
    bool verify_tls;
} AthenaWebSocket;

AthenaWebSocket *athena_websocket_connect(const char *url, bool verify_tls);
int athena_websocket_send(AthenaWebSocket *ws, const uint8_t *data, size_t len, bool binary);
int athena_websocket_recv(AthenaWebSocket *ws, uint8_t *buf, size_t buflen, size_t *out_len);
void athena_websocket_close(AthenaWebSocket *ws);
void athena_websocket_destroy(AthenaWebSocket *ws);

#endif /* ATHENA_WEBSOCKET_H */
