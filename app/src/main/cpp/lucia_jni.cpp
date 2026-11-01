#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "equip_core.h"
#include "cg_unlock_core.h"
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
    int cap = 1 << 20;
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

JNIEXPORT jstring JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeCgUnlock(JNIEnv *env, jobject, jint pid,
                                                             jstring roles, jstring donor,
                                                             jint dry, jstring snap) {
    const char *pr = roles ? env->GetStringUTFChars(roles, nullptr) : nullptr;
    const char *pd = donor ? env->GetStringUTFChars(donor, nullptr) : nullptr;
    const char *ps = snap ? env->GetStringUTFChars(snap, nullptr) : nullptr;
    int cap = 32768;
    char *buf = (char *) malloc(cap);
    if (!buf) {
        if (pr) env->ReleaseStringUTFChars(roles, pr);
        if (pd) env->ReleaseStringUTFChars(donor, pd);
        if (ps) env->ReleaseStringUTFChars(snap, ps);
        return env->NewStringUTF("{\"err\":-99}");
    }
    buf[0] = 0;
    int r = lt_cg_unlock((pid_t) pid, pr ? pr : "", pd ? pd : "H150_04",
                         (int) dry, ps ? ps : "", buf, cap);
    if (r < 0) snprintf(buf, (size_t) cap, "{\"err\":%d}", r);
    jstring out = env->NewStringUTF(buf);
    free(buf);
    if (pr) env->ReleaseStringUTFChars(roles, pr);
    if (pd) env->ReleaseStringUTFChars(donor, pd);
    if (ps) env->ReleaseStringUTFChars(snap, ps);
    return out;
}

JNIEXPORT jstring JNICALL
Java_com_openrubi_luciatools_mem_NativeBridge_nativeCgRestore(JNIEnv *env, jobject, jint pid,
                                                              jstring snap) {
    const char *ps = snap ? env->GetStringUTFChars(snap, nullptr) : nullptr;
    int cap = 4096;
    char *buf = (char *) malloc(cap);
    if (!buf) {
        if (ps) env->ReleaseStringUTFChars(snap, ps);
        return env->NewStringUTF("{\"err\":-99}");
    }
    buf[0] = 0;
    int r = lt_cg_restore((pid_t) pid, ps ? ps : "", buf, cap);
    if (r < 0) snprintf(buf, (size_t) cap, "{\"err\":%d}", r);
    jstring out = env->NewStringUTF(buf);
    free(buf);
    if (ps) env->ReleaseStringUTFChars(snap, ps);
    return out;
}

} // extern "C"