#include <stdlib.h>

#include <athena/websocket.h>

AthenaWebSocket *athena_websocket_connect(const char *url, bool verify_tls)
{
    AthenaWebSocket *ws;

    if (!url)
        return NULL;

    ws = calloc(1, sizeof(*ws));
    if (!ws)
        return NULL;

    ws->verify_tls = verify_tls;
    ws->ctx = ath_ws_connect(url, verify_tls);
    if (!ws->ctx) {
        free(ws);
        return NULL;
    }

    return ws;
}

int athena_websocket_send(AthenaWebSocket *ws, const uint8_t *data, size_t len, bool binary)
{
    if (!ws || !ws->ctx || !data)
        return -1;
    return ath_ws_send(ws->ctx, data, len, binary ? 1 : 0);
}

int athena_websocket_recv(AthenaWebSocket *ws, uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (!ws || !ws->ctx || !buf)
        return -1;
    return ath_ws_recv(ws->ctx, buf, buflen, out_len);
}

void athena_websocket_close(AthenaWebSocket *ws)
{
    if (!ws || !ws->ctx)
        return;
    ath_ws_close(ws->ctx);
    ws->ctx = NULL;
}

void athena_websocket_destroy(AthenaWebSocket *ws)
{
    if (!ws)
        return;
    athena_websocket_close(ws);
    free(ws);
}
