#include <jni.h>
#include <windows.h>
#include <stdlib.h>

#define YGHV_DEVICE L"\\\\.\\YuanGuardHV"

static DWORD ioctl_code(int fn) {
    return ((DWORD)0x5947 << 16) | ((DWORD)fn << 2);
}

static int collect_committed_pages(HANDLE proc, jlong *pages, int max_pages,
                                   int want_image);

JNIEXPORT jlong JNICALL
Java_YghvCtl_openHandle(JNIEnv *env, jclass cls) {
    HANDLE h = CreateFileW(YGHV_DEVICE, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    (void)env;
    (void)cls;
    if (h == INVALID_HANDLE_VALUE || h == NULL)
        return 0;
    return (jlong)(intptr_t)h;
}

JNIEXPORT void JNICALL
Java_YghvCtl_closeHandle(JNIEnv *env, jclass cls, jlong handle) {
    (void)env;
    (void)cls;
    if (handle != 0)
        CloseHandle((HANDLE)(intptr_t)handle);
}

JNIEXPORT jint JNICALL
Java_YghvCtl_lastError(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return (jint)GetLastError();
}

JNIEXPORT jint JNICALL
Java_YghvCtl_ioctl(JNIEnv *env, jclass cls, jlong handle, jint fn,
                   jbyteArray in, jbyteArray out) {
    BYTE *in_buf = NULL;
    BYTE *out_buf = NULL;
    DWORD in_len = 0;
    DWORD out_len = 0;
    DWORD returned = 0;
    BOOL ok;
    DWORD err;
    (void)cls;

    if (in) {
        in_buf = (BYTE *)(*env)->GetByteArrayElements(env, in, NULL);
        in_len = (DWORD)(*env)->GetArrayLength(env, in);
    }
    if (out) {
        out_buf = (BYTE *)(*env)->GetByteArrayElements(env, out, NULL);
        out_len = (DWORD)(*env)->GetArrayLength(env, out);
    }

    ok = DeviceIoControl((HANDLE)(intptr_t)handle, ioctl_code(fn),
                         in_buf, in_len, out_buf, out_len, &returned, NULL);
    err = ok ? 0 : GetLastError();

    if (in)
        (*env)->ReleaseByteArrayElements(env, in, (jbyte *)in_buf, JNI_ABORT);
    if (out)
        (*env)->ReleaseByteArrayElements(env, out, (jbyte *)out_buf, 0);
    return (jint)err;
}

JNIEXPORT jlongArray JNICALL
Java_YghvCtl_enumeratePages(JNIEnv *env, jclass cls, jint pid, jint max_pages) {
    HANDLE proc;
    jlong *pages;
    int count = 0;
    jlongArray result;
    (void)cls;

    proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                       FALSE, (DWORD)pid);
    if (!proc)
        return NULL;

    pages = (jlong *)calloc(max_pages, sizeof(jlong));
    if (!pages) {
        CloseHandle(proc);
        return NULL;
    }

    count = collect_committed_pages(proc, pages, max_pages, 1);
    if (count < max_pages)
        count += collect_committed_pages(proc, pages + count,
                                         max_pages - count, 0);

    CloseHandle(proc);
    result = (*env)->NewLongArray(env, count);
    if (result)
        (*env)->SetLongArrayRegion(env, result, 0, count, pages);
    free(pages);
    return result;
}

static int collect_committed_pages(HANDLE proc, jlong *pages, int max_pages,
                                   int want_image) {
    unsigned char *addr = NULL;
    int count = 0;

    while (count < max_pages) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T n = VirtualQueryEx(proc, addr, &mbi, sizeof(mbi));
        SIZE_T k;
        if (n == 0)
            break;
        if ((mbi.Type == MEM_IMAGE) != (want_image != 0))
            goto next;
        if (mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS
            && !(mbi.Protect & PAGE_GUARD)) {
            unsigned char *base = (unsigned char *)mbi.BaseAddress;
            SIZE_T region_pages = mbi.RegionSize / 4096;
            for (k = 0; k < region_pages && count < max_pages; k++)
                pages[count++] = (jlong)(uintptr_t)(base + k * 4096);
        }
next:
        addr = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.RegionSize == 0)
            break;
    }
    return count;
}
