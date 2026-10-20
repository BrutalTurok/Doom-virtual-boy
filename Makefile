# Path to the v810-gcc toolchain (e.g. VUEngine-Studio-Tools/linux/gcc/bin)
TC      ?= /opt/v810/bin
# Real-hardware build (the only supported target). VB_ROM1WAIT = cartridge ROM
# with 1 wait state, tested on a HyperFlash32.
REAL    ?= 1
CC      = $(TC)/v810-gcc
AS      = $(TC)/v810-as
OBJCOPY = $(TC)/v810-objcopy
LD_SCRIPT ?= vb/real.ld
BUILD   ?= build
OUT     ?= doom_vb
EXTRA   ?= -DVB_ROM1WAIT
CFLAGS  = -std=gnu11 -O2 -mv810 -Iinclude -DVB $(if $(REAL),-DVB_REAL) -ffreestanding -fno-builtin-printf $(EXTRA) -Wno-unused
EXCLUDE = source/lprintf.c $(if $(REAL),source/m_recip.c)
SRCS    = $(filter-out $(EXCLUDE),$(wildcard source/*.c)) vb/i_system_vb.c vb/syscalls.c $(if $(REAL),vb/vb_fb.c vb/vb_cmap_rot.c vb/vbprof.c vb/vb_sound.c vb/vb_sound_tables.c vb/vb_audio_data.c)
OBJS    = $(patsubst %.c,$(BUILD)/%.o,$(SRCS)) $(BUILD)/vb/crt0.o $(if $(REAL),$(BUILD)/vb/vb_timer_isr.o)

all: $(OUT).vb

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/source/doom_iwad.o: source/doom_iwad.c
	@mkdir -p $(dir $@)
	@$(CC) -std=gnu11 -O0 -mv810 -Iinclude $(if $(REAL),-DVB_REAL) -c $< -o $@

$(BUILD)/vb/vb_timer_isr.o: vb/vb_timer_isr.s
	@mkdir -p $(dir $@)
	@$(CC) -x assembler-with-cpp $(EXTRA) -c -o $@ $<

$(BUILD)/vb/crt0.o: vb/crt0.s
	@mkdir -p $(dir $@)
	@$(AS) -o $@ $<

$(OUT).elf: $(OBJS) $(LD_SCRIPT)
	$(CC) -o $@ -nostartfiles $(OBJS) -T$(LD_SCRIPT) -Wl,-Map,$(OUT).map -Wl,--start-group -lm -lc -lgcc -Wl,--end-group

$(OUT).vb: $(OUT).elf
	$(OBJCOPY) -O binary $< $@
	@ls -la $@

-include $(OBJS:.o=.d)

clean:
	rm -rf $(BUILD) $(OUT).*
