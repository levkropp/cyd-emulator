/*
 * Comprehensive emulator benchmark & validation firmware.
 * Tests every major subsystem that the emulator stubs or accelerates.
 * Useful for validating native stubs and as a baseline for JIT work.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha1.h"
#include "mbedtls/md5.h"

static const char *TAG = "bench";
static volatile uint32_t sink;

/* ---- helpers ---- */
static int64_t bench_start(void) { return esp_timer_get_time(); }
static void bench_end(const char *name, int64_t start, int iters) {
    int64_t us = esp_timer_get_time() - start;
    ESP_LOGI(TAG, "%-30s %6lld us  (%d iters)", name, (long long)us, iters);
}

/* ============================================================
 * Memory operations (tests native memcpy/memset/memmove stubs)
 * ============================================================ */
static uint8_t membuf_a[8192], membuf_b[8192];

static void bench_memset(void) {
    int64_t t = bench_start();
    for (int i = 0; i < 2000; i++) {
        memset(membuf_a, (uint8_t)i, sizeof(membuf_a));
        sink = membuf_a[100];
    }
    bench_end("memset 8KB x2000", t, 2000);
}

static void bench_memcpy(void) {
    memset(membuf_a, 0xAB, sizeof(membuf_a));
    int64_t t = bench_start();
    for (int i = 0; i < 2000; i++) {
        memcpy(membuf_b, membuf_a, sizeof(membuf_a));
        sink = membuf_b[100];
    }
    bench_end("memcpy 8KB x2000", t, 2000);
}

static void bench_memmove_overlap(void) {
    memset(membuf_a, 0xCD, sizeof(membuf_a));
    int64_t t = bench_start();
    for (int i = 0; i < 2000; i++) {
        memmove(membuf_a + 100, membuf_a, sizeof(membuf_a) - 100);
        sink = membuf_a[200];
    }
    bench_end("memmove overlap 8KB x2000", t, 2000);
}

/* ============================================================
 * String operations (tests native strlen/strcmp/strcpy stubs)
 * ============================================================ */
static char strbuf_a[512], strbuf_b[512];

static void bench_strlen(void) {
    memset(strbuf_a, 'X', 511); strbuf_a[511] = 0;
    int64_t t = bench_start();
    volatile size_t total = 0;
    for (int i = 0; i < 20000; i++)
        total += strlen(strbuf_a);
    sink = (uint32_t)total;
    bench_end("strlen 511B x20000", t, 20000);
}

static void bench_strcmp(void) {
    memset(strbuf_a, 'A', 511); strbuf_a[511] = 0;
    memset(strbuf_b, 'A', 511); strbuf_b[511] = 0;
    int64_t t = bench_start();
    volatile int result = 0;
    for (int i = 0; i < 20000; i++)
        result += strcmp(strbuf_a, strbuf_b);
    sink = (uint32_t)result;
    bench_end("strcmp equal 511B x20000", t, 20000);
}

static void bench_strcpy(void) {
    memset(strbuf_a, 'Z', 255); strbuf_a[255] = 0;
    int64_t t = bench_start();
    for (int i = 0; i < 20000; i++) {
        strcpy(strbuf_b, strbuf_a);
        sink = (uint32_t)strbuf_b[0];
    }
    bench_end("strcpy 255B x20000", t, 20000);
}

/* ============================================================
 * mbedtls SHA-256 (tests native SHA256 acceleration)
 * ============================================================ */
static void bench_sha256(void) {
    uint8_t data[256], hash[32];
    memset(data, 0x42, sizeof(data));
    int64_t t = bench_start();
    for (int i = 0; i < 1000; i++) {
        { mbedtls_sha256_context c; mbedtls_sha256_init(&c);
          mbedtls_sha256_starts(&c, 0); mbedtls_sha256_update(&c, data, sizeof(data));
          mbedtls_sha256_finish(&c, hash); mbedtls_sha256_free(&c); }
        sink = hash[0];
    }
    bench_end("SHA256 256B x1000", t, 1000);
}

static void bench_sha256_large(void) {
    static uint8_t bigdata[4096];
    uint8_t hash[32];
    memset(bigdata, 0x55, sizeof(bigdata));
    int64_t t = bench_start();
    for (int i = 0; i < 200; i++) {
        { mbedtls_sha256_context c; mbedtls_sha256_init(&c);
          mbedtls_sha256_starts(&c, 0); mbedtls_sha256_update(&c, bigdata, sizeof(bigdata));
          mbedtls_sha256_finish(&c, hash); mbedtls_sha256_free(&c); }
        sink = hash[0];
    }
    bench_end("SHA256 4KB x200", t, 200);
}

/* ============================================================
 * mbedtls SHA-1 (tests native SHA1 acceleration)
 * ============================================================ */
static void bench_sha1(void) {
    uint8_t data[256], hash[20];
    memset(data, 0x33, sizeof(data));
    int64_t t = bench_start();
    for (int i = 0; i < 1000; i++) {
        { mbedtls_sha1_context c; mbedtls_sha1_init(&c);
          mbedtls_sha1_starts(&c); mbedtls_sha1_update(&c, data, sizeof(data));
          mbedtls_sha1_finish(&c, hash); mbedtls_sha1_free(&c); }
        sink = hash[0];
    }
    bench_end("SHA1 256B x1000", t, 1000);
}

/* ============================================================
 * mbedtls MD5 (tests native MD5 acceleration)
 * ============================================================ */
static void bench_md5(void) {
    uint8_t data[256], hash[16];
    memset(data, 0x77, sizeof(data));
    int64_t t = bench_start();
    for (int i = 0; i < 1000; i++) {
        { mbedtls_md5_context c; mbedtls_md5_init(&c);
          mbedtls_md5_starts(&c); mbedtls_md5_update(&c, data, sizeof(data));
          mbedtls_md5_finish(&c, hash); mbedtls_md5_free(&c); }
        sink = hash[0];
    }
    bench_end("MD5 256B x1000", t, 1000);
}

/* ============================================================
 * Integer arithmetic
 * ============================================================ */
static void bench_intmath(void) {
    int64_t t = bench_start();
    uint32_t a = 0x12345678, b = 0x9ABCDEF0, c = 0;
    for (int i = 0; i < 200000; i++) {
        c += a * b;
        a = (a >> 7) | (a << 25);
        b ^= c;
        c += a / (b | 1);
    }
    sink = c;
    bench_end("intmath 200K iters", t, 200000);
}

/* ============================================================
 * Floating point
 * ============================================================ */
static void bench_float(void) {
    int64_t t = bench_start();
    volatile float x = 1.0f, y = 0.5f;
    for (int i = 0; i < 100000; i++) {
        x = x * 1.0001f + y;
        y = y * 0.9999f - 0.001f;
        if (x > 1e6f) x = 1.0f;
        if (y < -1e6f) y = 0.5f;
    }
    { float xv = x; uint32_t tmp; memcpy(&tmp, &xv, 4); sink = tmp; }
    bench_end("float mul+add 100K", t, 100000);
}

/* ============================================================
 * Sorting (branch-heavy + memory-heavy)
 * ============================================================ */
static int cmp_u32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t*)a, vb = *(const uint32_t*)b;
    return (va > vb) - (va < vb);
}

static void bench_qsort(void) {
    static uint32_t arr[512];
    uint32_t x = 0xDEADBEEF;
    int64_t t = bench_start();
    for (int rep = 0; rep < 200; rep++) {
        for (int i = 0; i < 512; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            arr[i] = x;
        }
        qsort(arr, 512, sizeof(uint32_t), cmp_u32);
    }
    sink = arr[0];
    bench_end("qsort 512 elems x200", t, 200);
}

/* ============================================================
 * CRC32 (byte-at-a-time table lookup)
 * ============================================================ */
static uint32_t crc_table[256];
static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc_table[i] = c;
    }
}
static uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t*)data;
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

static void bench_crc32(void) {
    static uint8_t buf[2048];
    memset(buf, 0x42, sizeof(buf));
    crc32_init();
    int64_t t = bench_start();
    uint32_t result = 0;
    for (int i = 0; i < 1000; i++)
        result ^= crc32(buf, sizeof(buf));
    sink = result;
    bench_end("CRC32 2KB x1000", t, 1000);
}

/* ============================================================
 * SHA256 correctness validation
 * ============================================================ */
static void validate_sha256(void) {
    /* Test vector: SHA256("abc") */
    const uint8_t input[] = "abc";
    const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    uint8_t hash[32];
    { mbedtls_sha256_context c; mbedtls_sha256_init(&c);
      mbedtls_sha256_starts(&c, 0); mbedtls_sha256_update(&c, input, 3);
      mbedtls_sha256_finish(&c, hash); mbedtls_sha256_free(&c); }
    if (memcmp(hash, expected, 32) == 0)
        ESP_LOGI(TAG, "SHA256(\"abc\") PASS");
    else
        ESP_LOGE(TAG, "SHA256(\"abc\") FAIL!");
}

/* ============================================================
 * SHA1 correctness validation
 * ============================================================ */
static void validate_sha1(void) {
    const uint8_t input[] = "abc";
    const uint8_t expected[20] = {
        0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,
        0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,
        0x9c,0xd0,0xd8,0x9d
    };
    uint8_t hash[20];
    { mbedtls_sha1_context c; mbedtls_sha1_init(&c);
      mbedtls_sha1_starts(&c); mbedtls_sha1_update(&c, input, 3);
      mbedtls_sha1_finish(&c, hash); mbedtls_sha1_free(&c); }
    if (memcmp(hash, expected, 20) == 0)
        ESP_LOGI(TAG, "SHA1(\"abc\")  PASS");
    else
        ESP_LOGE(TAG, "SHA1(\"abc\")  FAIL!");
}

/* ============================================================
 * MD5 correctness validation
 * ============================================================ */
static void validate_md5(void) {
    const uint8_t input[] = "abc";
    const uint8_t expected[16] = {
        0x90,0x01,0x50,0x98,0x3c,0xd2,0x4f,0xb0,
        0xd6,0x96,0x3f,0x7d,0x28,0xe1,0x7f,0x72
    };
    uint8_t hash[16];
    { mbedtls_md5_context c; mbedtls_md5_init(&c);
      mbedtls_md5_starts(&c); mbedtls_md5_update(&c, input, 3);
      mbedtls_md5_finish(&c, hash); mbedtls_md5_free(&c); }
    if (memcmp(hash, expected, 16) == 0)
        ESP_LOGI(TAG, "MD5(\"abc\")   PASS");
    else
        ESP_LOGE(TAG, "MD5(\"abc\")   FAIL!");
}

/* ============================================================
 * Main
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== Emulator Benchmark & Validation Suite ===");
    ESP_LOGI(TAG, "");

    /* Correctness checks first */
    ESP_LOGI(TAG, "--- Crypto Validation ---");
    validate_sha256();
    validate_sha1();
    validate_md5();
    ESP_LOGI(TAG, "");

    /* Benchmarks */
    ESP_LOGI(TAG, "--- Memory Operations ---");
    bench_memset();
    bench_memcpy();
    bench_memmove_overlap();
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "--- String Operations ---");
    bench_strlen();
    bench_strcmp();
    bench_strcpy();
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "--- Crypto ---");
    bench_sha256();
    bench_sha256_large();
    bench_sha1();
    bench_md5();
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "--- Compute ---");
    bench_intmath();
    bench_float();
    bench_qsort();
    bench_crc32();
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "=== Benchmark complete ===");

    while (1) vTaskDelay(1000 / portTICK_PERIOD_MS);
}
