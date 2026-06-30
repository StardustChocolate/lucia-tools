#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include "equip_core.h"
#include "rubimem.h"

extern "C" {

JNIEXPORT jint JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeFindPid(JNIEnv *env, jobject, jstring jpkg) {
    const char *pkg = env->GetStringUTFChars(jpkg, nullptr);
    pid_t pid = rm_find_pid_by_name(pkg);
    env->ReleaseStringUTFChars(jpkg, pkg);
    return (jint) pid;
}

JNIEXPORT jboolean JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeCanConnect(JNIEnv *, jobject, jint pid) {
    return ec_can_connect((pid_t) pid) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeExportEquipment(JNIEnv *env, jobject, jint pid) {
    int cap = 1 << 20; /* 1MB JSON 缂撳啿 */
    char *buf = (char *) malloc(cap);
    if (!buf) return env->NewStringUTF("{\"error\":\"oom\"}");
    int rc = ec_export_equipment((pid_t) pid, buf, cap);
    jstring result;
    if (rc < 0) {
        char err[64];
        snprintf(err, sizeof(err), "{\"error\":%d}", rc);
        result = env->NewStringUTF(err);
    } else {
        result = env->NewStringUTF(buf);
    }
    free(buf);
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeExportAccount(JNIEnv *env, jobject, jint pid) {
    int cap = 4096;
    char *buf = (char *) malloc(cap);
    if (!buf) return env->NewStringUTF("{\"found\":0}");
    int rc = ec_export_account((pid_t) pid, buf, cap);
    jstring result;
    if (rc < 0) {
        char err[64];
        snprintf(err, sizeof(err), "{\"found\":0,\"error\":%d}", rc);
        result = env->NewStringUTF(err);
    } else {
        result = env->NewStringUTF(buf);
    }
    free(buf);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeSetInstakill(JNIEnv *, jobject, jint pid, jboolean enable) {
    return (jint) ec_set_instakill((pid_t) pid, enable ? 1 : 0);
}

JNIEXPORT jint JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativePrimeInstakill(JNIEnv *, jobject, jint pid) {
    return (jint) ec_prime_instakill((pid_t) pid);
}

JNIEXPORT jint JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativePrimeExport(JNIEnv *, jobject, jint pid) {
    return (jint) ec_prime_export((pid_t) pid);
}

} // extern "C"
