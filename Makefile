CC = gcc
EXEC = ki

KI_DIR = /usr/local/share/ki

COMMON_SRCS = $(wildcard src/common/*.c)
UTILITY_SRCS = $(wildcard src/utilities/*.c)
FRONTEND_SRCS = $(wildcard src/frontend/*.c)
IR_SRCS = $(wildcard src/ir/*.c)
DOCUMENTOR_SRCS = $(wildcard src/documentor/*.c)

# At some point we want to accept an argument to decide between backends,
# but right now we only support Linux x86_64, so we'll just hardcode it.
BACKEND_SRCS = src/backend/backends/linux_x86_64/backend.c src/backend/backends/linux_x86_64/backend_utilities.c

SRCS = $(wildcard src/*.c) $(COMMON_SRCS) $(UTILITY_SRCS) $(FRONTEND_SRCS) $(IR_SRCS) $(DOCUMENTOR_SRCS) $(BACKEND_SRCS)
INCS = -Isrc/common -Isrc/frontend -Isrc/ir -Isrc/backend -Isrc/utilities -Isrc/documentor -Isrc
LIBS = -lm

CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -march=native
DEBUG ?= 0

ifeq ($(DEBUG),1)
CFLAGS += -g -Wl,-z,now -Wl,-z,relro \
	  -fsanitize=undefined,address \
	  -fstack-protector-strong \
	  -ftrampolines \
	  -ftrivial-auto-var-init=pattern
else
CFLAGS += -s -O3 -DNDEBUG
endif

.PHONY: all clean install uninstall

all: $(EXEC)

$(EXEC): $(SRCS)
	$(CC) $(INCS) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f ./$(EXEC) *.asm *.o a.out

install:
	make
	mkdir -p $(KI_DIR)
	cp -r ./lib $(KI_DIR)/

uninstall:
	make clean
	rm -r $(KI_DIR)
