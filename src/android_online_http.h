#ifndef PVG3_ANDROID_ONLINE_HTTP_H
#define PVG3_ANDROID_ONLINE_HTTP_H
#include <jni.h>
/* Call before a room is opened. JNI attaches only the background net thread;
 * HTTPS certificate validation uses Android's platform trust store. */
void android_online_set_vm(JavaVM *vm);
#endif
