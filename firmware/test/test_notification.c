// firmware/test/test_notification.c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../main/notification.h"

static notification_store_t store;

static void test_init_empty(void) {
    notif_store_init(&store);
    assert(notif_store_count(&store) == 0);
    assert(notif_store_get(&store, 0) == NULL);
    printf("  PASS: test_init_empty\n");
}

static void test_add_one(void) {
    notif_store_init(&store);
    int rc = notif_store_add(&store, "s1", "my-project", "Waiting for input");
    assert(rc == 0);
    assert(notif_store_count(&store) == 1);
    const notification_t *n = notif_store_get(&store, 0);
    assert(n != NULL);
    assert(strcmp(n->id, "s1") == 0);
    assert(strcmp(n->project, "my-project") == 0);
    assert(strcmp(n->message, "Waiting for input") == 0);
    printf("  PASS: test_add_one\n");
}

static void test_dismiss(void) {
    notif_store_init(&store);
    notif_store_add(&store, "s1", "proj", "msg");
    notif_store_add(&store, "s2", "proj", "msg");
    assert(notif_store_count(&store) == 2);

    int rc = notif_store_dismiss(&store, "s1");
    assert(rc == 0);
    assert(notif_store_count(&store) == 1);

    // Dismiss nonexistent is idempotent
    rc = notif_store_dismiss(&store, "s1");
    assert(rc == -1);
    assert(notif_store_count(&store) == 1);
    printf("  PASS: test_dismiss\n");
}

static void test_clear(void) {
    notif_store_init(&store);
    notif_store_add(&store, "s1", "p", "m");
    notif_store_add(&store, "s2", "p", "m");
    notif_store_clear(&store);
    assert(notif_store_count(&store) == 0);
    printf("  PASS: test_clear\n");
}

static void test_update_existing(void) {
    notif_store_init(&store);
    notif_store_add(&store, "s1", "proj", "first");
    notif_store_add(&store, "s1", "proj", "updated");
    assert(notif_store_count(&store) == 1);
    const notification_t *n = notif_store_get(&store, 0);
    assert(strcmp(n->message, "updated") == 0);
    printf("  PASS: test_update_existing\n");
}

static void test_overflow_drops_oldest(void) {
    notif_store_init(&store);
    char id[8];
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        snprintf(id, sizeof(id), "s%d", i);
        notif_store_add(&store, id, "p", "m");
    }
    assert(notif_store_count(&store) == NOTIF_MAX_COUNT);

    // Add 9th — should drop "s0"
    notif_store_add(&store, "s_new", "p", "m");
    assert(notif_store_count(&store) == NOTIF_MAX_COUNT);

    // "s0" should be gone
    int found_s0 = 0;
    int found_new = 0;
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        const notification_t *n = notif_store_get(&store, i);
        if (n && strcmp(n->id, "s0") == 0) found_s0 = 1;
        if (n && strcmp(n->id, "s_new") == 0) found_new = 1;
    }
    assert(!found_s0);
    assert(found_new);
    printf("  PASS: test_overflow_drops_oldest\n");
}

int main(void) {
    printf("Running notification store tests...\n");
    test_init_empty();
    test_add_one();
    test_dismiss();
    test_clear();
    test_update_existing();
    test_overflow_drops_oldest();
    printf("All tests passed!\n");
    return 0;
}
