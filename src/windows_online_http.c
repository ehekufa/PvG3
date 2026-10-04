/* Firebase RTDB HTTPS transport for the standalone Windows build.
 * Requests stay on the worker thread in online_net.c and are restricted to
 * the same public rooms/catalog paths as the Android transport. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include "online_net.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define FIREBASE_HOST L"pvg3-ae824-default-rtdb.firebaseio.com"

static int safe_path(const char *path) {
    static const char *const roots[] = {"rooms", "levels", "levels-index"};
    if (!path) return 0;
    int allowed = 0;
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; ++i) {
        size_t n = strlen(roots[i]);
        if (!strncmp(path, roots[i], n) &&
            (path[n] == '/' || path[n] == '.')) {
            allowed = 1;
            break;
        }
    }
    size_t n = strlen(path);
    if (!allowed || n < 9 || n > 88 || strstr(path, "..") ||
        strstr(path, "//") || strcmp(path + n - 5, ".json")) return 0;
    for (size_t i = 0; i < n; ++i) {
        char c = path[i];
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
            (c < '0' || c > '9') && c != '/' && c != '_' && c != '.' && c != '-')
            return 0;
    }
    return 1;
}

static int widen_ascii(const char *src, wchar_t *dst, size_t capacity) {
    if (!src || !dst || !capacity) return 0;
    size_t n = strlen(src);
    if (n >= capacity) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c > 0x7f) return 0;
        dst[i] = (wchar_t)c;
    }
    dst[n] = 0;
    return 1;
}

int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t response_cap) {
    if (response && response_cap) response[0] = 0;
    if (!safe_path(path) || !method || !response || response_cap < 2 ||
        (strcmp(method, "GET") && strcmp(method, "PUT") &&
         strcmp(method, "DELETE"))) return -1;

    wchar_t wide_path[96], wide_method[12], wide_etag[160];
    wchar_t headers[512];
    if (!widen_ascii(path, wide_path, sizeof wide_path / sizeof wide_path[0]) ||
        !widen_ascii(method, wide_method,
                     sizeof wide_method / sizeof wide_method[0])) return -1;
    wide_etag[0] = 0;
    if (if_match && !widen_ascii(if_match, wide_etag,
                                 sizeof wide_etag / sizeof wide_etag[0])) return -1;

    int written = swprintf(headers, sizeof headers / sizeof headers[0],
        L"Accept: application/json\r\nCache-Control: no-cache\r\n");
    if (written < 0) return -1;
    size_t used_header = (size_t)written;
    if (if_match) {
        written = swprintf(headers + used_header,
            sizeof headers / sizeof headers[0] - used_header,
            L"If-Match: %ls\r\n", wide_etag);
        if (written < 0) return -1;
        used_header += (size_t)written;
    }
    size_t body_len = body && !strcmp(method, "PUT") ? strlen(body) : 0;
    if (body_len >= ON_LEVEL_JSON_CAP || body_len > UINT32_MAX) return -1;
    if (body_len) {
        written = swprintf(headers + used_header,
            sizeof headers / sizeof headers[0] - used_header,
            L"Content-Type: application/json; charset=utf-8\r\n");
        if (written < 0) return -1;
        used_header += (size_t)written;
    }
    if (used_header >= sizeof headers / sizeof headers[0]) return -1;

    HINTERNET session = WinHttpOpen(L"PvG3/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return -1;
    int level_transfer = !strncmp(path, "levels/", 7);
    int timeout_ms = level_transfer ? 60000 : 7500;
    WinHttpSetTimeouts(session, 7500, 7500, timeout_ms, timeout_ms);
    HINTERNET connection = WinHttpConnect(session, FIREBASE_HOST,
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return -1;
    }
    wchar_t object_name[100];
    int n = swprintf(object_name, sizeof object_name / sizeof object_name[0],
                     L"/%ls", wide_path);
    if (n < 0 || (size_t)n >= sizeof object_name / sizeof object_name[0]) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return -1;
    }
    HINTERNET request = WinHttpOpenRequest(connection, wide_method, object_name,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return -1;
    }

    DWORD body_size = (DWORD)body_len;
    LPCVOID body_bytes = body_len ? (LPCVOID)body : WINHTTP_NO_REQUEST_DATA;
    int result = -1;
    if (WinHttpSendRequest(request, headers, (DWORD)-1,
                           (LPVOID)body_bytes, body_size, body_size, 0) &&
        WinHttpReceiveResponse(request, NULL)) {
        DWORD status = 0, status_size = sizeof status;
        if (WinHttpQueryHeaders(request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                WINHTTP_NO_HEADER_INDEX)) {
            result = (int)status;
            /* Firebase echoes successful PUT values. The level publisher only
             * needs the status, so do not copy a large level back into memory. */
            if (!(level_transfer && !strcmp(method, "PUT") && result < 400)) {
                size_t used = 0;
                for (;;) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available)) {
                        result = -1;
                        break;
                    }
                    if (!available) break;
                    if ((size_t)available >= response_cap - used) {
                        result = -2;
                        break;
                    }
                    DWORD received = 0;
                    if (!WinHttpReadData(request, response + used, available,
                                         &received)) {
                        result = -1;
                        break;
                    }
                    if (!received) break;
                    used += received;
                }
                response[used] = 0;
            }
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}
