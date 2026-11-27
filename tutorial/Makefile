# If you would like to choose a different path to the SDK, you can pass it as an
# argument.
ifndef MICROKIT_SDK
	MICROKIT_SDK := ../../microkit-sdk-2.4.0
endif

# In case the default compiler triple doesn't work for you or your package manager
# only has aarch64-none-elf or something, you can specifiy the toolchain.
ifndef TOOLCHAIN
	# Get whether the common toolchain triples exist
	TOOLCHAIN_AARCH64_LINUX_GNU := $(shell command -v aarch64-linux-gnu-gcc 2> /dev/null)
	TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU := $(shell command -v aarch64-unknown-linux-gnu-gcc 2> /dev/null)
	TOOLCHAIN_AARCH64_NONE_ELF := $(shell command -v aarch64-none-elf-gcc 2> /dev/null)
	# Then check if they are defined and select the appropriate one
	ifdef TOOLCHAIN_AARCH64_LINUX_GNU
		TOOLCHAIN := aarch64-linux-gnu
	else ifdef TOOLCHAIN_AARCH64_UNKNOWN_LINUX_GNU
		TOOLCHAIN := aarch64-unknown-linux-gnu
	else ifdef TOOLCHAIN_AARCH64_NONE_ELF
		TOOLCHAIN := aarch64-none-elf
	else
		$(error "Could not find an AArch64 cross-compiler")
	endif
endif

BOARD := qemu_virt_aarch64
MICROKIT_CONFIG := debug
BUILD_DIR := build

CPU := cortex-a53

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

PRINTF_OBJS := printf.o util.o
GAME_OBJS := $(PRINTF_OBJS) game.o
WORDLE_SERVER_OBJS := $(PRINTF_OBJS) wordle_server.o
VMM_OBJS := $(PRINTF_OBJS) vmm.o psci.o smc.o fault.o cpuif.o vgic.o global_data.o vgic_v2.o

BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(MICROKIT_CONFIG)

IMAGES := game.elf wordle_server.elf vmm.elf
# Note that these warnings being disabled is to avoid compilation errors while in the middle of completing each exercise part
CFLAGS := -mcpu=$(CPU) -mstrict-align -nostdlib -fno-stack-protector -ffreestanding -g -Wall -Wno-array-bounds -Wno-unused-variable -Wno-unused-function -Werror -I$(BOARD_DIR)/include -Ivmm/src/util -Iinclude -DBOARD_$(BOARD)
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

# VMM defines
KERNEL_IMAGE = vmm/images/linux
DTB_IMAGE = vmm/images/linux.dtb
INITRD_IMAGE = vmm/images/rootfs.cpio.gz

all: directories $(IMAGE_FILE)

directories:
	$(info $(shell mkdir -p $(BUILD_DIR)))

run: $(IMAGE_FILE)
	qemu-system-aarch64 -machine virt,virtualization=on \
		-cpu $(CPU) \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-netdev user,id=mynet0 \
		-device virtio-net-device,netdev=mynet0,mac=52:55:00:d1:55:01

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: vmm/src/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: vmm/src/util/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: vmm/src/vgic/%.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/global_data.o: vmm/src/global_data.S $(KERNEL_IMAGE) $(INITRD_IMAGE) $(DTB_IMAGE)
	$(CC) -c -g -x assembler-with-cpp \
					-DVM_KERNEL_IMAGE_PATH=\"$(KERNEL_IMAGE)\" \
					-DVM_DTB_IMAGE_PATH=\"$(DTB_IMAGE)\" \
					-DVM_INITRD_IMAGE_PATH=\"$(INITRD_IMAGE)\" \
					$< -o $@

$(BUILD_DIR)/game.elf: $(addprefix $(BUILD_DIR)/, $(GAME_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/wordle_server.elf: $(addprefix $(BUILD_DIR)/, $(WORDLE_SERVER_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/vmm.elf: $(addprefix $(BUILD_DIR)/, $(VMM_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) wordle.system
	$(MICROKIT_TOOL) wordle.system --search-path $(BUILD_DIR) --board $(BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)
