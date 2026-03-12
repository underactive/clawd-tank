# firmware/test/Makefile
CC = gcc
CFLAGS = -Wall -Wextra -I../main

test: test_notification
	./test_notification

test_notification: test_notification.c ../main/notification.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_notification

.PHONY: test clean
