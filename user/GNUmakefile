.SUFFIXES:

.DELETE_ON_ERROR:

CC := cc
LD := ld

CFLAGS := \
	-g \
	-O2 \
	-Wall \
	-Wextra \
	-std=gnu11 \
	-m64 \
	-ffreestanding \
	-fno-builtin \
	-fno-PIC \
	-fno-pie \
	-fno-stack-protector \
	-mno-red-zone \
	-I include \
	-I ../abi \
	-MMD \
	-MP

USER_CFLAGS += \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mgeneral-regs-only \
	-fno-tree-vectorize

ASFLAGS := \
	-g \
	-m64 \
	-ffreestanding \
	-fno-PIC \
	-fno-pie \
	-I include \
	-I ../abi

LDFLAGS := \
	-m elf_x86_64 \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	-z noexecstack \
	-T linker.ld

COMMON_CFILES := \
	lib/syscall.c

COMMON_ASFILES := \
	crt/start.S \
	lib/syscall.S

TEST_CFILES := \
	apps/test/main.c

DESKTOP_CFILES := \
	lib/graphics.c \
	apps/desktop/main.c

DESKTOP_ASFILES := \
	apps/desktop/assets.S

COMMON_OBJECTS := \
	$(addprefix obj/,$(COMMON_CFILES:.c=.c.o)) \
	$(addprefix obj/,$(COMMON_ASFILES:.S=.S.o))

TEST_OBJECTS := \
	$(COMMON_OBJECTS) \
	$(addprefix obj/,$(TEST_CFILES:.c=.c.o))

DESKTOP_OBJECTS := \
	$(COMMON_OBJECTS) \
	$(addprefix obj/,$(DESKTOP_CFILES:.c=.c.o)) \
	$(addprefix obj/,$(DESKTOP_ASFILES:.S=.S.o))

DEPENDENCIES := \
	$(addprefix obj/,$(COMMON_CFILES:.c=.c.d)) \
	$(addprefix obj/,$(TEST_CFILES:.c=.c.d)) \
	$(addprefix obj/,$(DESKTOP_CFILES:.c=.c.d))

.PHONY: all
all: \
	bin/user_test.elf \
	bin/desktop.elf

-include $(DEPENDENCIES)

bin/user_test.elf: \
	$(TEST_OBJECTS) \
	linker.ld \
	GNUmakefile
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(TEST_OBJECTS) -o $@

bin/desktop.elf: \
	$(DESKTOP_OBJECTS) \
	linker.ld \
	GNUmakefile
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(DESKTOP_OBJECTS) -o $@

obj/%.c.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(USER_CFLAGS) -c $< -o $@

obj/%.S.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(ASFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf bin obj