BINOUT := ./bin/
PATHSRC := ./src/
TARGET_NAME := ge4game
TARGET := $(BINOUT)$(TARGET_NAME)
CC := psp-gcc

PSPSDK := $(shell psp-config --pspsdk-path)

CFLAGS := -Os -G0 -Wall -Wextra -Werror -MMD -MP -I$(PSPSDK)/include -D_PSP_FW_VERSION=660
ASFLAGS := -x assembler-with-cpp $(CFLAGS)
LDFLAGS := -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx -nostartfiles \
	-Wl,-zmax-page-size=128 -L$(PSPSDK)/lib
OBJS := $(BINOUT)ge.o $(BINOUT)main.o $(BINOUT)exports.o
DEPS := $(OBJS:.o=.d)
LIBS := -lpspsysmem_kernel_660
FINAL_TARGET := $(TARGET).prx

.PHONY: all release clean

all: $(FINAL_TARGET)
release: all

$(BINOUT):
	mkdir -p $@

$(OBJS): | $(BINOUT)

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

%.prx: %.elf
	psp-prxgen $< $@

$(BINOUT)%.o: $(PATHSRC)%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINOUT)%.o: $(PATHSRC)%.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(PATHSRC)exports.c: $(PATHSRC)exports.exp
	psp-build-exports -b $< > $@

$(BINOUT)exports.o: $(PATHSRC)exports.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINOUT)main.o: $(PATHSRC)wrap.h

-include $(DEPS)

clean:
	-rm -f $(FINAL_TARGET) $(TARGET).elf $(OBJS) $(DEPS) $(PATHSRC)exports.c
