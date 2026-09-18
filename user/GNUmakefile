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

CFILES := \
	lib/syscall.c \
	apps/test/main.c

ASFILES := \
	crt/start.S \
	lib/syscall.S

OBJECTS := \
	$(addprefix obj/,$(CFILES:.c=.c.o)) \
	$(addprefix obj/,$(ASFILES:.S=.S.o))

DEPENDENCIES := \
	$(addprefix obj/,$(CFILES:.c=.c.d))

.PHONY: all
all: bin/user_test.elf

-include $(DEPENDENCIES)

bin/user_test.elf: $(OBJECTS) linker.ld GNUmakefile
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

obj/%.c.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c $< -o $@

obj/%.S.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(ASFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf bin obj