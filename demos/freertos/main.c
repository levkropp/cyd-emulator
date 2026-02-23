/*
 * main.c -- FreeRTOS primitives test suite for CYD emulator
 *
 * Exercises tasks, semaphores, queues, event groups, timers,
 * and critical sections.  Displays PASS/FAIL for each test.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "display.h"
#include "touch.h"
#include "font.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"

static const char *TAG = "test";

/* ---- Display helpers ---- */

#define COL_PASS  0x07E0  /* green  */
#define COL_FAIL  0xF800  /* red    */
#define COL_RUN   0xFFE0  /* yellow */
#define COL_HEAD  0x07FF  /* cyan   */
#define COL_FG    0xFFFF  /* white  */
#define COL_BG    0x0000  /* black  */
#define COL_DIM   0x7BEF  /* gray   */

static int test_row = 0;
static int pass_count = 0;
static int fail_count = 0;

static void test_header(const char *title)
{
    int y = test_row * FONT_HEIGHT;
    if (y + FONT_HEIGHT > DISPLAY_HEIGHT) return;
    display_fill_rect(0, y, DISPLAY_WIDTH, FONT_HEIGHT, COL_BG);
    display_string(0, y, title, COL_HEAD, COL_BG);
    test_row++;
}

static void test_status(const char *name, int running)
{
    int y = test_row * FONT_HEIGHT;
    if (y + FONT_HEIGHT > DISPLAY_HEIGHT) return;
    display_fill_rect(0, y, DISPLAY_WIDTH, FONT_HEIGHT, COL_BG);

    char line[41];
    memset(line, ' ', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    /* Copy name into line (max 30 chars) */
    int nlen = (int)strlen(name);
    if (nlen > 30) nlen = 30;
    memcpy(line + 1, name, nlen);

    if (running) {
        memcpy(line + 34, "...", 3);
        display_string(0, y, line, COL_RUN, COL_BG);
    } else {
        display_string(0, y, line, COL_FG, COL_BG);
    }
}

static void test_result(const char *name, int passed)
{
    int y = test_row * FONT_HEIGHT;
    if (y + FONT_HEIGHT > DISPLAY_HEIGHT) return;
    display_fill_rect(0, y, DISPLAY_WIDTH, FONT_HEIGHT, COL_BG);

    char line[41];
    memset(line, ' ', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    int nlen = (int)strlen(name);
    if (nlen > 30) nlen = 30;
    memcpy(line + 1, name, nlen);

    /* Dots between name and result */
    for (int i = 1 + nlen; i < 33; i++) line[i] = '.';

    if (passed) {
        memcpy(line + 33, "PASS", 4);
        display_string(0, y, line, COL_PASS, COL_BG);
        pass_count++;
    } else {
        memcpy(line + 33, "FAIL", 4);
        display_string(0, y, line, COL_FAIL, COL_BG);
        fail_count++;
    }
    test_row++;

    ESP_LOGI(TAG, "%s: %s", name, passed ? "PASS" : "FAIL");
}

/* ---- Test 1: Task creation & vTaskDelay ---- */

static volatile int task1_counter = 0;
static SemaphoreHandle_t task1_mutex;

static void counter_task(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < 10; i++) {
        xSemaphoreTake(task1_mutex, portMAX_DELAY);
        task1_counter++;
        xSemaphoreGive(task1_mutex);
        vTaskDelay(5);
    }
    ESP_LOGI(TAG, "Task %d done", id);
    vTaskDelete(NULL);
}

static int test_tasks(void)
{
    task1_counter = 0;
    task1_mutex = xSemaphoreCreateMutex();
    if (!task1_mutex) return 0;

    TaskHandle_t h1, h2, h3;
    BaseType_t r1 = xTaskCreate(counter_task, "cnt1", 2048,
                                 (void *)(intptr_t)1, 5, &h1);
    BaseType_t r2 = xTaskCreate(counter_task, "cnt2", 2048,
                                 (void *)(intptr_t)2, 5, &h2);
    BaseType_t r3 = xTaskCreatePinnedToCore(counter_task, "cnt3", 2048,
                                             (void *)(intptr_t)3, 5, &h3, 1);

    if (r1 != pdPASS || r2 != pdPASS || r3 != pdPASS) {
        vSemaphoreDelete(task1_mutex);
        return 0;
    }

    /* Wait for all tasks to finish */
    vTaskDelay(300);

    int result = (task1_counter == 30);
    if (!result)
        ESP_LOGI(TAG, "task counter=%d, expected 30", task1_counter);

    vSemaphoreDelete(task1_mutex);
    return result;
}

/* ---- Test 2: Mutex semaphore ---- */

static int test_mutex(void)
{
    SemaphoreHandle_t mtx = xSemaphoreCreateMutex();
    if (!mtx) return 0;

    /* Mutex starts available */
    BaseType_t r = xSemaphoreTake(mtx, 0);
    if (r != pdTRUE) { vSemaphoreDelete(mtx); return 0; }

    /* Second take should timeout (non-recursive) */
    /* Actually, from same thread, pthreads default mutex might deadlock.
     * Use a timed take with 0 timeout to test. */
    r = xSemaphoreTake(mtx, 0);
    /* Should fail — mutex already held */
    if (r == pdTRUE) { vSemaphoreDelete(mtx); return 0; }

    /* Give it back */
    xSemaphoreGive(mtx);

    /* Now should be takeable again */
    r = xSemaphoreTake(mtx, 0);
    if (r != pdTRUE) { vSemaphoreDelete(mtx); return 0; }

    xSemaphoreGive(mtx);
    vSemaphoreDelete(mtx);
    return 1;
}

/* ---- Test 3: Recursive mutex ---- */

static int test_recursive_mutex(void)
{
    SemaphoreHandle_t mtx = xSemaphoreCreateRecursiveMutex();
    if (!mtx) return 0;

    /* Take recursively 3 times */
    for (int i = 0; i < 3; i++) {
        BaseType_t r = xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
        if (r != pdTRUE) { vSemaphoreDelete(mtx); return 0; }
    }

    /* Give back 3 times */
    for (int i = 0; i < 3; i++) {
        xSemaphoreGiveRecursive(mtx);
    }

    /* Should be available again */
    BaseType_t r = xSemaphoreTakeRecursive(mtx, 0);
    if (r != pdTRUE) { vSemaphoreDelete(mtx); return 0; }
    xSemaphoreGiveRecursive(mtx);

    vSemaphoreDelete(mtx);
    return 1;
}

/* ---- Test 4: Binary semaphore ---- */

static volatile int binsem_received = 0;
static SemaphoreHandle_t binsem;

static void binsem_waiter(void *arg)
{
    (void)arg;
    /* Wait for the semaphore */
    xSemaphoreTake(binsem, portMAX_DELAY);
    binsem_received = 1;
    vTaskDelete(NULL);
}

static int test_binary_semaphore(void)
{
    binsem = xSemaphoreCreateBinary();
    if (!binsem) return 0;
    binsem_received = 0;

    TaskHandle_t th;
    xTaskCreate(binsem_waiter, "bwait", 2048, NULL, 5, &th);

    /* Let waiter block */
    vTaskDelay(50);
    if (binsem_received) { vSemaphoreDelete(binsem); return 0; }

    /* Signal it */
    xSemaphoreGive(binsem);
    vTaskDelay(50);

    int result = binsem_received;
    vSemaphoreDelete(binsem);
    return result;
}

/* ---- Test 5: Counting semaphore ---- */

static int test_counting_semaphore(void)
{
    SemaphoreHandle_t sem = xSemaphoreCreateCounting(3, 0);
    if (!sem) return 0;

    /* Give 3 times */
    for (int i = 0; i < 3; i++)
        xSemaphoreGive(sem);

    /* Fourth give should fail (max=3) */
    BaseType_t r = xSemaphoreGive(sem);
    if (r == pdTRUE) { vSemaphoreDelete(sem); return 0; }

    /* Take 3 times */
    for (int i = 0; i < 3; i++) {
        r = xSemaphoreTake(sem, 0);
        if (r != pdTRUE) { vSemaphoreDelete(sem); return 0; }
    }

    /* Fourth take should fail (count=0) */
    r = xSemaphoreTake(sem, 0);
    if (r == pdTRUE) { vSemaphoreDelete(sem); return 0; }

    vSemaphoreDelete(sem);
    return 1;
}

/* ---- Test 6: Queue send/receive ---- */

static int test_queue(void)
{
    QueueHandle_t q = xQueueCreate(5, sizeof(int));
    if (!q) return 0;

    /* Send 5 items */
    for (int i = 0; i < 5; i++) {
        int val = i * 10;
        BaseType_t r = xQueueSend(q, &val, 0);
        if (r != pdTRUE) { vQueueDelete(q); return 0; }
    }

    /* Queue full — next send should fail */
    int dummy = 99;
    if (xQueueSend(q, &dummy, 0) == pdTRUE) { vQueueDelete(q); return 0; }

    /* Check count */
    if (uxQueueMessagesWaiting(q) != 5) { vQueueDelete(q); return 0; }
    if (uxQueueSpacesAvailable(q) != 0) { vQueueDelete(q); return 0; }

    /* Receive in FIFO order */
    for (int i = 0; i < 5; i++) {
        int val;
        BaseType_t r = xQueueReceive(q, &val, 0);
        if (r != pdTRUE || val != i * 10) { vQueueDelete(q); return 0; }
    }

    /* Queue empty — next receive should fail */
    if (xQueueReceive(q, &dummy, 0) == pdTRUE) { vQueueDelete(q); return 0; }

    vQueueDelete(q);
    return 1;
}

/* ---- Test 7: Queue peek ---- */

static int test_queue_peek(void)
{
    QueueHandle_t q = xQueueCreate(3, sizeof(int));
    if (!q) return 0;

    int val = 42;
    xQueueSend(q, &val, 0);

    /* Peek should return 42 without removing it */
    int peeked;
    BaseType_t r = xQueuePeek(q, &peeked, 0);
    if (r != pdTRUE || peeked != 42) { vQueueDelete(q); return 0; }

    /* Item should still be there */
    if (uxQueueMessagesWaiting(q) != 1) { vQueueDelete(q); return 0; }

    /* Receive should also return 42 */
    int received;
    r = xQueueReceive(q, &received, 0);
    if (r != pdTRUE || received != 42) { vQueueDelete(q); return 0; }

    /* Now empty */
    if (uxQueueMessagesWaiting(q) != 0) { vQueueDelete(q); return 0; }

    vQueueDelete(q);
    return 1;
}

/* ---- Test 8: Queue send-to-front ---- */

static int test_queue_front(void)
{
    QueueHandle_t q = xQueueCreate(5, sizeof(int));
    if (!q) return 0;

    int v1 = 1, v2 = 2, v3 = 3;
    xQueueSend(q, &v1, 0);       /* queue: [1] */
    xQueueSend(q, &v2, 0);       /* queue: [1, 2] */
    xQueueSendToFront(q, &v3, 0); /* queue: [3, 1, 2] */

    int out;
    xQueueReceive(q, &out, 0);
    if (out != 3) { vQueueDelete(q); return 0; }

    xQueueReceive(q, &out, 0);
    if (out != 1) { vQueueDelete(q); return 0; }

    xQueueReceive(q, &out, 0);
    if (out != 2) { vQueueDelete(q); return 0; }

    vQueueDelete(q);
    return 1;
}

/* ---- Test 9: Queue cross-task ---- */

static QueueHandle_t cross_q;

static void queue_sender(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) {
        int val = i + 100;
        xQueueSend(cross_q, &val, portMAX_DELAY);
        vTaskDelay(10);
    }
    vTaskDelete(NULL);
}

static int test_queue_cross_task(void)
{
    cross_q = xQueueCreate(5, sizeof(int));
    if (!cross_q) return 0;

    TaskHandle_t th;
    xTaskCreate(queue_sender, "qsend", 2048, NULL, 5, &th);

    int ok = 1;
    for (int i = 0; i < 5; i++) {
        int val;
        BaseType_t r = xQueueReceive(cross_q, &val, pdMS_TO_TICKS(500));
        if (r != pdTRUE || val != i + 100) { ok = 0; break; }
    }

    vTaskDelay(50);  /* let sender task exit */
    vQueueDelete(cross_q);
    return ok;
}

/* ---- Test 10: Event group set/wait ---- */

#define EVT_BIT_A  (1 << 0)
#define EVT_BIT_B  (1 << 1)
#define EVT_BIT_C  (1 << 2)

static EventGroupHandle_t test_evg;

static void evg_setter_task(void *arg)
{
    (void)arg;
    vTaskDelay(30);
    xEventGroupSetBits(test_evg, EVT_BIT_A);
    vTaskDelay(30);
    xEventGroupSetBits(test_evg, EVT_BIT_B);
    vTaskDelay(30);
    xEventGroupSetBits(test_evg, EVT_BIT_C);
    vTaskDelete(NULL);
}

static int test_event_group(void)
{
    test_evg = xEventGroupCreate();
    if (!test_evg) return 0;

    TaskHandle_t th;
    xTaskCreate(evg_setter_task, "evgset", 2048, NULL, 5, &th);

    /* Wait for all 3 bits, clear on exit */
    EventBits_t bits = xEventGroupWaitBits(test_evg,
        EVT_BIT_A | EVT_BIT_B | EVT_BIT_C,
        pdTRUE,    /* clear on exit */
        pdTRUE,    /* wait for all */
        pdMS_TO_TICKS(2000));

    int result = ((bits & (EVT_BIT_A | EVT_BIT_B | EVT_BIT_C)) ==
                  (EVT_BIT_A | EVT_BIT_B | EVT_BIT_C));

    /* Bits should be cleared after wait */
    if (result) {
        EventBits_t after = xEventGroupGetBits(test_evg);
        if (after & (EVT_BIT_A | EVT_BIT_B | EVT_BIT_C))
            result = 0;
    }

    vTaskDelay(50);
    vEventGroupDelete(test_evg);
    return result;
}

/* ---- Test 11: Event group wait-any ---- */

static int test_event_group_any(void)
{
    EventGroupHandle_t evg = xEventGroupCreate();
    if (!evg) return 0;

    xEventGroupSetBits(evg, EVT_BIT_B);

    /* Wait for any of A|B|C, don't clear */
    EventBits_t bits = xEventGroupWaitBits(evg,
        EVT_BIT_A | EVT_BIT_B | EVT_BIT_C,
        pdFALSE,   /* don't clear */
        pdFALSE,   /* wait for any */
        0);

    int result = ((bits & EVT_BIT_B) != 0);

    /* B should still be set (no clear on exit) */
    if (result) {
        EventBits_t after = xEventGroupGetBits(evg);
        if (!(after & EVT_BIT_B))
            result = 0;
    }

    vEventGroupDelete(evg);
    return result;
}

/* ---- Test 12: One-shot timer ---- */

static volatile int oneshot_fired = 0;

static void oneshot_cb(TimerHandle_t t)
{
    (void)t;
    oneshot_fired = 1;
}

static int test_timer_oneshot(void)
{
    oneshot_fired = 0;
    TimerHandle_t t = xTimerCreate("oneshot", pdMS_TO_TICKS(50),
                                    pdFALSE, NULL, oneshot_cb);
    if (!t) return 0;

    xTimerStart(t, 0);

    /* Wait for it to fire */
    vTaskDelay(200);

    int result = oneshot_fired;

    /* Reset and verify it doesn't fire again */
    oneshot_fired = 0;
    vTaskDelay(200);
    if (oneshot_fired) result = 0;

    xTimerDelete(t, 0);
    return result;
}

/* ---- Test 13: Periodic timer ---- */

static volatile int periodic_count = 0;

static void periodic_cb(TimerHandle_t t)
{
    (void)t;
    periodic_count++;
}

static int test_timer_periodic(void)
{
    periodic_count = 0;
    TimerHandle_t t = xTimerCreate("periodic", pdMS_TO_TICKS(50),
                                    pdTRUE, NULL, periodic_cb);
    if (!t) return 0;

    xTimerStart(t, 0);
    vTaskDelay(280);  /* should fire ~5 times (50ms * 5 = 250ms) */
    xTimerStop(t, 0);

    /* Allow some tolerance: 4-7 fires */
    int result = (periodic_count >= 4 && periodic_count <= 7);
    if (!result)
        ESP_LOGI(TAG, "periodic count=%d, expected 4-7", periodic_count);

    xTimerDelete(t, 0);
    return result;
}

/* ---- Test 14: Timer ID ---- */

static volatile int timer_id_ok = 0;

static void timer_id_cb(TimerHandle_t t)
{
    int *id = (int *)pvTimerGetTimerID(t);
    if (id && *id == 42)
        timer_id_ok = 1;
}

static int test_timer_id(void)
{
    timer_id_ok = 0;
    int my_id = 42;
    TimerHandle_t t = xTimerCreate("idtest", pdMS_TO_TICKS(30),
                                    pdFALSE, &my_id, timer_id_cb);
    if (!t) return 0;

    xTimerStart(t, 0);
    vTaskDelay(150);

    int result = timer_id_ok;
    xTimerDelete(t, 0);
    return result;
}

/* ---- Test 15: Critical sections ---- */

static volatile int critical_counter = 0;
static portMUX_TYPE critical_mux __attribute__((unused)) = portMUX_INITIALIZER_UNLOCKED;

static void critical_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        taskENTER_CRITICAL(&critical_mux);
        critical_counter++;
        taskEXIT_CRITICAL(&critical_mux);
    }
    vTaskDelete(NULL);
}

static int test_critical_section(void)
{
    critical_counter = 0;

    TaskHandle_t h1, h2;
    xTaskCreate(critical_task, "crit1", 2048, NULL, 5, &h1);
    xTaskCreate(critical_task, "crit2", 2048, NULL, 5, &h2);

    vTaskDelay(500);

    int result = (critical_counter == 2000);
    if (!result)
        ESP_LOGI(TAG, "critical counter=%d, expected 2000",
                 critical_counter);
    return result;
}

/* ---- Test 16: Tick count ---- */

static int test_tick_count(void)
{
    TickType_t t1 = xTaskGetTickCount();
    vTaskDelay(100);
    TickType_t t2 = xTaskGetTickCount();

    /* Elapsed should be ~100ms (ticks = ms).  Allow 80-200 tolerance. */
    int elapsed = (int)(t2 - t1);
    int result = (elapsed >= 80 && elapsed <= 200);
    if (!result)
        ESP_LOGI(TAG, "tick elapsed=%d, expected ~100", elapsed);
    return result;
}

/* ---- Test 17: Queue overwrite (length-1 queue) ---- */

static int test_queue_overwrite(void)
{
    QueueHandle_t q = xQueueCreate(1, sizeof(int));
    if (!q) return 0;

    int val = 10;
    xQueueOverwrite(q, &val);

    val = 20;
    xQueueOverwrite(q, &val);  /* overwrites previous */

    int out;
    BaseType_t r = xQueueReceive(q, &out, 0);
    if (r != pdTRUE || out != 20) { vQueueDelete(q); return 0; }

    vQueueDelete(q);
    return 1;
}

/* ---- Test 18: Queue reset ---- */

static int test_queue_reset(void)
{
    QueueHandle_t q = xQueueCreate(5, sizeof(int));
    if (!q) return 0;

    int val = 1;
    xQueueSend(q, &val, 0);
    xQueueSend(q, &val, 0);
    xQueueSend(q, &val, 0);

    if (uxQueueMessagesWaiting(q) != 3) { vQueueDelete(q); return 0; }

    xQueueReset(q);

    if (uxQueueMessagesWaiting(q) != 0) { vQueueDelete(q); return 0; }

    vQueueDelete(q);
    return 1;
}

/* ---- Test 19: esp_timer one-shot ---- */

static volatile int esp_timer_oneshot_fired = 0;

static void esp_timer_oneshot_cb(void *arg)
{
    (void)arg;
    esp_timer_oneshot_fired = 1;
}

static int test_esp_timer_oneshot(void)
{
    esp_timer_oneshot_fired = 0;
    esp_timer_handle_t t;
    esp_timer_create_args_t args = {
        .callback = esp_timer_oneshot_cb,
        .name = "test_once",
    };
    if (esp_timer_create(&args, &t) != ESP_OK) return 0;
    if (esp_timer_start_once(t, 50000) != ESP_OK) { esp_timer_delete(t); return 0; }

    vTaskDelay(200);
    int result = esp_timer_oneshot_fired;

    esp_timer_delete(t);
    return result;
}

/* ---- Test 20: esp_timer periodic ---- */

static volatile int esp_timer_periodic_count = 0;

static void esp_timer_periodic_cb(void *arg)
{
    (void)arg;
    esp_timer_periodic_count++;
}

static int test_esp_timer_periodic(void)
{
    esp_timer_periodic_count = 0;
    esp_timer_handle_t t;
    esp_timer_create_args_t args = {
        .callback = esp_timer_periodic_cb,
        .name = "test_periodic",
    };
    if (esp_timer_create(&args, &t) != ESP_OK) return 0;
    if (esp_timer_start_periodic(t, 50000) != ESP_OK) { esp_timer_delete(t); return 0; }

    vTaskDelay(280);
    esp_timer_stop(t);

    /* Expect 4-7 fires (50ms period over 280ms) */
    int result = (esp_timer_periodic_count >= 4 && esp_timer_periodic_count <= 7);
    if (!result)
        ESP_LOGI(TAG, "esp_timer periodic count=%d, expected 4-7",
                 esp_timer_periodic_count);

    esp_timer_delete(t);
    return result;
}

/* ---- Test 21: esp_timer_get_time ---- */

static int test_esp_timer_get_time(void)
{
    int64_t t1 = esp_timer_get_time();
    vTaskDelay(100);
    int64_t t2 = esp_timer_get_time();

    int64_t elapsed_us = t2 - t1;
    /* Should be ~100000 us.  Allow 80000-200000. */
    int result = (elapsed_us >= 80000 && elapsed_us <= 200000);
    if (!result)
        ESP_LOGI(TAG, "esp_timer elapsed=%lld us, expected ~100000",
                 (long long)elapsed_us);
    return result;
}

/* ---- Test 22: NVS set/get integers ---- */

static int test_nvs_integers(void)
{
    nvs_handle_t h;
    if (nvs_open("test_int", NVS_READWRITE, &h) != ESP_OK) return 0;

    /* Erase any previous test data */
    nvs_erase_all(h);

    nvs_set_i32(h, "val_i32", -12345);
    nvs_set_u32(h, "val_u32", 99999);
    nvs_set_u8(h, "val_u8", 42);
    nvs_commit(h);

    int32_t i32_out;
    uint32_t u32_out;
    uint8_t u8_out;

    if (nvs_get_i32(h, "val_i32", &i32_out) != ESP_OK || i32_out != -12345) {
        nvs_close(h); return 0;
    }
    if (nvs_get_u32(h, "val_u32", &u32_out) != ESP_OK || u32_out != 99999) {
        nvs_close(h); return 0;
    }
    if (nvs_get_u8(h, "val_u8", &u8_out) != ESP_OK || u8_out != 42) {
        nvs_close(h); return 0;
    }

    /* Non-existent key should return NOT_FOUND */
    int32_t dummy;
    if (nvs_get_i32(h, "nope", &dummy) != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h); return 0;
    }

    nvs_close(h);
    return 1;
}

/* ---- Test 23: NVS set/get string ---- */

static int test_nvs_string(void)
{
    nvs_handle_t h;
    if (nvs_open("test_str", NVS_READWRITE, &h) != ESP_OK) return 0;

    nvs_erase_all(h);
    nvs_set_str(h, "greeting", "Hello CYD!");
    nvs_commit(h);

    /* Query length first (out_value=NULL) */
    size_t len = 0;
    if (nvs_get_str(h, "greeting", NULL, &len) != ESP_OK || len == 0) {
        nvs_close(h); return 0;
    }

    char buf[64];
    len = sizeof(buf);
    if (nvs_get_str(h, "greeting", buf, &len) != ESP_OK) {
        nvs_close(h); return 0;
    }

    int result = (strcmp(buf, "Hello CYD!") == 0);
    nvs_close(h);
    return result;
}

/* ---- Test 24: NVS persistence (close and reopen) ---- */

static int test_nvs_persistence(void)
{
    /* Write */
    nvs_handle_t h;
    if (nvs_open("test_persist", NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_erase_all(h);
    nvs_set_u32(h, "magic", 0xDEADBEEF);
    nvs_commit(h);
    nvs_close(h);

    /* Reopen and read */
    if (nvs_open("test_persist", NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t val;
    int result = (nvs_get_u32(h, "magic", &val) == ESP_OK && val == 0xDEADBEEF);
    nvs_close(h);
    return result;
}

/* ---- Test 25: NVS erase key ---- */

static int test_nvs_erase(void)
{
    nvs_handle_t h;
    if (nvs_open("test_erase", NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_erase_all(h);

    nvs_set_u32(h, "a", 1);
    nvs_set_u32(h, "b", 2);
    nvs_commit(h);

    nvs_erase_key(h, "a");
    nvs_commit(h);

    uint32_t val;
    int result = (nvs_get_u32(h, "a", &val) == ESP_ERR_NVS_NOT_FOUND);
    if (result)
        result = (nvs_get_u32(h, "b", &val) == ESP_OK && val == 2);

    nvs_close(h);
    return result;
}

/* ---- Test 26: esp_random ---- */

static int test_esp_random(void)
{
    /* Generate several random numbers, verify they're not all the same */
    uint32_t vals[8];
    for (int i = 0; i < 8; i++)
        vals[i] = esp_random();

    int different = 0;
    for (int i = 1; i < 8; i++) {
        if (vals[i] != vals[0]) { different = 1; break; }
    }

    /* Also test esp_fill_random */
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    esp_fill_random(buf, sizeof(buf));

    int nonzero = 0;
    for (int i = 0; i < 16; i++) {
        if (buf[i] != 0) { nonzero = 1; break; }
    }

    return different && nonzero;
}

/* ---- Test 27: esp_system basics ---- */

static int test_esp_system(void)
{
    /* Reset reason should be POWERON */
    if (esp_reset_reason() != ESP_RST_POWERON) return 0;

    /* Heap sizes should be reasonable */
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    if (free_heap == 0 || min_heap == 0) return 0;
    if (min_heap > free_heap) return 0;

    return 1;
}

/* ---- Test 28: GPIO set/get level ---- */

static int test_gpio(void)
{
    /* Configure pin 21 as output */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << 21),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) return 0;

    /* Default level should be 0 */
    if (gpio_get_level(GPIO_NUM_21) != 0) return 0;

    /* Set high and read back */
    gpio_set_level(GPIO_NUM_21, 1);
    if (gpio_get_level(GPIO_NUM_21) != 1) return 0;

    /* Set low and read back */
    gpio_set_level(GPIO_NUM_21, 0);
    if (gpio_get_level(GPIO_NUM_21) != 0) return 0;

    /* Test a different pin */
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);
    if (gpio_get_level(GPIO_NUM_2) != 1) return 0;

    /* Pin 21 should still be 0 */
    if (gpio_get_level(GPIO_NUM_21) != 0) return 0;

    return 1;
}

/* ---- Test 29: LEDC duty cycle ---- */

static int test_ledc(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_cfg) != ESP_OK) return 0;

    ledc_channel_config_t ch_cfg = {
        .gpio_num = 21,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 4096,
        .hpoint = 0,
    };
    if (ledc_channel_config(&ch_cfg) != ESP_OK) return 0;

    /* Initial duty from channel config */
    if (ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != 4096) return 0;

    /* Set new duty */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8000);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != 8000) return 0;

    /* Different channel should be independent */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1000);
    if (ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1) != 1000) return 0;
    if (ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != 8000) return 0;

    return 1;
}

/* ---- Test 30: ADC read ---- */

static int test_adc(void)
{
    /* Configure 12-bit width */
    if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) return 0;
    if (adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11) != ESP_OK) return 0;

    /* Should return midpoint for 12-bit (2048) */
    int raw = adc1_get_raw(ADC1_CHANNEL_0);
    if (raw != 2048) return 0;

    /* Switch to 10-bit, should return 512 */
    adc1_config_width(ADC_WIDTH_BIT_10);
    raw = adc1_get_raw(ADC1_CHANNEL_0);
    if (raw != 512) return 0;

    return 1;
}

/* ---- Main ---- */

void app_main(void)
{
    display_init();
    touch_init();

    display_clear(COL_BG);

    ESP_LOGI(TAG, "FreeRTOS test suite starting");

    test_header(" FreeRTOS Test Suite");
    test_row++;  /* blank line */

    /* --- Tasks --- */
    test_header(" Tasks");
    test_status("xTaskCreate + mutex", 1);
    test_result("xTaskCreate + mutex", test_tasks());
    test_status("xTaskGetTickCount", 1);
    test_result("xTaskGetTickCount", test_tick_count());
    test_status("Critical sections", 1);
    test_result("Critical sections", test_critical_section());

    /* --- Semaphores --- */
    test_header(" Semaphores");
    test_status("Mutex take/give", 1);
    test_result("Mutex take/give", test_mutex());
    test_status("Recursive mutex", 1);
    test_result("Recursive mutex", test_recursive_mutex());
    test_status("Binary semaphore", 1);
    test_result("Binary semaphore", test_binary_semaphore());
    test_status("Counting semaphore", 1);
    test_result("Counting semaphore", test_counting_semaphore());

    /* --- Queues --- */
    test_header(" Queues");
    test_status("Send/receive FIFO", 1);
    test_result("Send/receive FIFO", test_queue());
    test_status("Peek", 1);
    test_result("Peek", test_queue_peek());
    test_status("Send-to-front", 1);
    test_result("Send-to-front", test_queue_front());
    test_status("Cross-task queue", 1);
    test_result("Cross-task queue", test_queue_cross_task());

    /* Page 2 - scroll down or we run out of space at row 15 */
    /* At this point test_row should be around 15, near bottom.
     * Continue on same screen — 320x240 fits 15 rows of 16px. */

    test_status("Overwrite", 1);
    test_result("Overwrite", test_queue_overwrite());
    test_status("Reset", 1);
    test_result("Reset", test_queue_reset());

    /* We're at ~17 rows, which is 272px - won't fit 240px.
     * Let's wait for a tap then show page 2. */
    vTaskDelay(2000);
    display_clear(COL_BG);
    test_row = 0;

    /* --- Event groups --- */
    test_header(" Event Groups");
    test_status("Wait-all + clear", 1);
    test_result("Wait-all + clear", test_event_group());
    test_status("Wait-any", 1);
    test_result("Wait-any", test_event_group_any());

    /* --- FreeRTOS Timers --- */
    test_header(" FreeRTOS Timers");
    test_status("One-shot timer", 1);
    test_result("One-shot timer", test_timer_oneshot());
    test_status("Periodic timer", 1);
    test_result("Periodic timer", test_timer_periodic());
    test_status("Timer ID", 1);
    test_result("Timer ID", test_timer_id());

    /* --- ESP-IDF APIs (page 3) --- */
    vTaskDelay(2000);
    display_clear(COL_BG);
    test_row = 0;

    test_header(" esp_timer");
    test_status("One-shot", 1);
    test_result("One-shot", test_esp_timer_oneshot());
    test_status("Periodic", 1);
    test_result("Periodic", test_esp_timer_periodic());
    test_status("get_time", 1);
    test_result("get_time", test_esp_timer_get_time());

    test_header(" NVS");
    test_status("Integer set/get", 1);
    test_result("Integer set/get", test_nvs_integers());
    test_status("String set/get", 1);
    test_result("String set/get", test_nvs_string());
    test_status("Persistence", 1);
    test_result("Persistence", test_nvs_persistence());
    test_status("Erase key", 1);
    test_result("Erase key", test_nvs_erase());

    test_header(" esp_system");
    test_status("esp_random", 1);
    test_result("esp_random", test_esp_random());
    test_status("System basics", 1);
    test_result("System basics", test_esp_system());

    test_header(" GPIO / Peripherals");
    test_status("GPIO set/get", 1);
    test_result("GPIO set/get", test_gpio());
    test_status("LEDC duty cycle", 1);
    test_result("LEDC duty cycle", test_ledc());
    test_status("ADC read", 1);
    test_result("ADC read", test_adc());

    /* --- Summary --- */
    test_row++;
    test_header(" Summary");
    {
        int y = test_row * FONT_HEIGHT;
        char summary[41];
        snprintf(summary, sizeof(summary), "  %d passed, %d failed",
                 pass_count, fail_count);
        display_string(0, y, summary,
                       fail_count == 0 ? COL_PASS : COL_FAIL, COL_BG);
        test_row++;
    }

    if (fail_count == 0) {
        int y = test_row * FONT_HEIGHT;
        display_string(0, y, "  All tests passed!", COL_PASS, COL_BG);
    }

    ESP_LOGI(TAG, "Done: %d passed, %d failed", pass_count, fail_count);

    /* Idle forever */
    while (1) {
        vTaskDelay(1000);
    }
}
