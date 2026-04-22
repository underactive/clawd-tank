# firmware/test/Makefile
CC = gcc
# ASan was dropped from the default build: Apple Clang on macOS 26.x (Tahoe)
# deadlocks inside AsanInitFromRtl during the libsystem_malloc initializer
# path (re-entrant spin on StaticSpinMutex::LockSlow) before main() runs.
# UBSan alone is unaffected and still catches the classes of bug we care
# about here. Re-add `,address` when a fixed asan runtime ships.
CFLAGS = -Wall -Wextra -fsanitize=undefined -Werror -I../main -Istubs
LDFLAGS = -fsanitize=undefined

test: test_notification test_config_store test_rle_decode
	./test_notification
	./test_config_store
	./test_rle_decode

test_notification: test_notification.c ../main/notification.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_config_store: test_config_store.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_rle_decode: test_rle_decode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f test_notification test_config_store test_rle_decode

.PHONY: test clean
