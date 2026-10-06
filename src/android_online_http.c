/* Firebase Realtime Database REST over Android's own HTTPS stack. No WebView,
 * external browser, insecure HTTP, embedded credentials or Java APK code.
 * Called only by online_net.c's background pthread, never from game_tick. */
#include "android_online_http.h"
#include "online_config.h"
#include "online_net.h"

#include <stdio.h>
#include <string.h>

#define BASE "https://" PVG3_DATABASE_HOST "/"
static JavaVM *vm;
void android_online_set_vm(JavaVM *jvm) { vm = jvm; }

static int good(JNIEnv *env) {
    if (!(*env)->ExceptionCheck(env)) return 1;
    (*env)->ExceptionClear(env);
    return 0;
}
static void property(JNIEnv *env, jobject conn, jmethodID setter,
                     const char *key, const char *value) {
    jstring k = (*env)->NewStringUTF(env, key);
    jstring v = (*env)->NewStringUTF(env, value);
    if (k && v) (*env)->CallVoidMethod(env, conn, setter, k, v);
    if (k) (*env)->DeleteLocalRef(env, k);
    if (v) (*env)->DeleteLocalRef(env, v);
}
/* Restrict HTTPS requests to the room protocol and public level catalog. */
static int safe_path(const char *path) {
    static const char *const roots[] = {"rooms", "levels", "levels-index",
        "accounts", "tokens", "admins", "bans", "comments"};
    if (!path) return 0;
    int allowed = 0;
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        size_t root_len = strlen(roots[i]);
        if (!strncmp(path, roots[i], root_len) &&
            (path[root_len] == '/' || path[root_len] == '.')) {
            allowed = 1;break;
        }
    }
    if (!allowed) return 0;
    size_t n = strlen(path);
    if (n < 9 || n > 88 || strstr(path, "..") || strstr(path, "//") ||
        strcmp(path + n - 5, ".json")) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = path[i];
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') &&
            (c < '0' || c > '9') && c != '/' && c != '_' && c != '.' && c != '-')
            return 0;
    }
    return 1;
}
int on_http_request(const char *path, const char *method, const char *body,
                    const char *if_match, char *response, size_t response_cap) {
    if (response && response_cap) response[0] = 0;
    if (!vm || !safe_path(path) || !method || !response || response_cap < 2 ||
        (strcmp(method, "GET") && strcmp(method, "PUT") && strcmp(method, "DELETE")))
        return -1;
    JNIEnv *env = NULL;
    int attached = 0, result = -1;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return -1;
        attached = 1;
    }
    jobject url = NULL, conn = NULL, stream = NULL, out = NULL;
    jclass url_class = NULL, http_class = NULL, output_class = NULL, input_class = NULL;
    jstring address = NULL, verb = NULL;
    jbyteArray bytes = NULL;
    char full[192];
    int n = snprintf(full, sizeof full, BASE "%s", path);
    if (n <= 0 || (size_t)n >= sizeof full) goto done;
    url_class = (*env)->FindClass(env, "java/net/URL");
    http_class = (*env)->FindClass(env, "java/net/HttpURLConnection");
    if (!url_class || !http_class || !good(env)) goto done;
    jmethodID url_ctor = (*env)->GetMethodID(env, url_class, "<init>", "(Ljava/lang/String;)V");
    jmethodID open = (*env)->GetMethodID(env, url_class, "openConnection", "()Ljava/net/URLConnection;");
    jmethodID set_method = (*env)->GetMethodID(env, http_class, "setRequestMethod", "(Ljava/lang/String;)V");
    jmethodID connect_timeout = (*env)->GetMethodID(env, http_class, "setConnectTimeout", "(I)V");
    jmethodID read_timeout = (*env)->GetMethodID(env, http_class, "setReadTimeout", "(I)V");
    jmethodID caches = (*env)->GetMethodID(env, http_class, "setUseCaches", "(Z)V");
    jmethodID header = (*env)->GetMethodID(env, http_class, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    jmethodID set_output = (*env)->GetMethodID(env, http_class, "setDoOutput", "(Z)V");
    jmethodID get_output = (*env)->GetMethodID(env, http_class, "getOutputStream", "()Ljava/io/OutputStream;");
    jmethodID status = (*env)->GetMethodID(env, http_class, "getResponseCode", "()I");
    jmethodID get_input = (*env)->GetMethodID(env, http_class, "getInputStream", "()Ljava/io/InputStream;");
    jmethodID get_error = (*env)->GetMethodID(env, http_class, "getErrorStream", "()Ljava/io/InputStream;");
    jmethodID disconnect = (*env)->GetMethodID(env, http_class, "disconnect", "()V");
    if (!url_ctor || !open || !set_method || !connect_timeout || !read_timeout ||
        !caches || !header || !set_output || !get_output || !status ||
        !get_input || !get_error || !disconnect || !good(env)) goto done;
    address = (*env)->NewStringUTF(env, full);
    verb = (*env)->NewStringUTF(env, method);
    if (!address || !verb || !good(env)) goto done;
    url = (*env)->NewObject(env, url_class, url_ctor, address);
    if (!url || !good(env)) goto done;
    conn = (*env)->CallObjectMethod(env, url, open);
    if (!conn || !good(env) || !(*env)->IsInstanceOf(env, conn, http_class)) goto done;
    (*env)->CallVoidMethod(env, conn, set_method, verb);
    int level_transfer = !strncmp(path, "levels/", 7);
    jint timeout_ms = level_transfer ? 60000 : 7500;
    (*env)->CallVoidMethod(env, conn, connect_timeout, timeout_ms);
    (*env)->CallVoidMethod(env, conn, read_timeout, timeout_ms);
    (*env)->CallVoidMethod(env, conn, caches, JNI_FALSE);
    property(env, conn, header, "Accept", "application/json");
    property(env, conn, header, "Cache-Control", "no-cache");
    if (if_match) property(env, conn, header, "If-Match", if_match);
    if (!good(env)) goto done;
    if (body && !strcmp(method, "PUT")) {
        size_t length = strlen(body);
        if (length >= ON_LEVEL_JSON_CAP) goto done;
        property(env, conn, header, "Content-Type", "application/json; charset=utf-8");
        (*env)->CallVoidMethod(env, conn, set_output, JNI_TRUE);
        if (!good(env)) goto done;
        out = (*env)->CallObjectMethod(env, conn, get_output);
        output_class = (*env)->FindClass(env, "java/io/OutputStream");
        if (!out || !output_class || !good(env)) goto done;
        jmethodID write = (*env)->GetMethodID(env, output_class, "write", "([BII)V");
        jmethodID close_out = (*env)->GetMethodID(env, output_class, "close", "()V");
        if (!write || !close_out || !good(env)) goto done;
        bytes = (*env)->NewByteArray(env, (jsize)length);
        if (!bytes || !good(env)) goto done;
        (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)length, (const jbyte *)body);
        if (!good(env)) goto done;
        (*env)->CallVoidMethod(env, out, write, bytes, 0, (jint)length);
        (*env)->CallVoidMethod(env, out, close_out);
        if (!good(env)) goto done;
        (*env)->DeleteLocalRef(env, bytes);bytes = NULL;
    }
    result = (*env)->CallIntMethod(env, conn, status);
    if (!good(env)) {result = -1;goto done;}
    /* Firebase echoes successful PUT values. The level publisher only needs
     * the status, so avoid buffering a second copy of a multi-megabyte level. */
    if (level_transfer && !strcmp(method, "PUT") && result < 400) goto done;
    stream = (*env)->CallObjectMethod(env, conn, result >= 400 ? get_error : get_input);
    if (!good(env)) {result = -1;goto done;}
    if (stream) {
        input_class = (*env)->FindClass(env, "java/io/InputStream");
        if (!input_class || !good(env)) {result = -1;goto done;}
        jmethodID read = (*env)->GetMethodID(env, input_class, "read", "([BII)I");
        jmethodID close_in = (*env)->GetMethodID(env, input_class, "close", "()V");
        if (!read || !close_in || !good(env)) {result = -1;goto done;}
        bytes = (*env)->NewByteArray(env, 4096);
        if (!bytes || !good(env)) {result = -1;goto done;}
        size_t used = 0;
        for (;;) {
            jint count = (*env)->CallIntMethod(env, stream, read, bytes, 0, 4096);
            if (!good(env)) {result = -1;break;}
            if (count < 0) break;
            if (count == 0) continue;
            if ((size_t)count >= response_cap - used) {result = -2;break;}
            (*env)->GetByteArrayRegion(env, bytes, 0, count, (jbyte *)(response + used));
            if (!good(env)) {result = -1;break;}
            used += (size_t)count;
        }
        response[used] = 0;
        (*env)->CallVoidMethod(env, stream, close_in);
    }
done:
    if ((*env)->ExceptionCheck(env)) {(*env)->ExceptionClear(env);result = -1;}
    if (conn) {
        jmethodID disconnect = (*env)->GetMethodID(env, http_class, "disconnect", "()V");
        if (disconnect) (*env)->CallVoidMethod(env, conn, disconnect);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }
    if (bytes) (*env)->DeleteLocalRef(env, bytes);
    if (input_class) (*env)->DeleteLocalRef(env, input_class);
    if (output_class) (*env)->DeleteLocalRef(env, output_class);
    if (stream) (*env)->DeleteLocalRef(env, stream);
    if (out) (*env)->DeleteLocalRef(env, out);
    if (conn) (*env)->DeleteLocalRef(env, conn);
    if (url) (*env)->DeleteLocalRef(env, url);
    if (verb) (*env)->DeleteLocalRef(env, verb);
    if (address) (*env)->DeleteLocalRef(env, address);
    if (http_class) (*env)->DeleteLocalRef(env, http_class);
    if (url_class) (*env)->DeleteLocalRef(env, url_class);
    if (attached) (*vm)->DetachCurrentThread(vm);
    return result;
}
