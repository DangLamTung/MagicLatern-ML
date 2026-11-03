# define the module name - make sure name is max 8 characters
MODULE_NAME = film_lv

# The LiveView preview grades with the Soup Engine look, not its own maths:
# the same .fs stock, compiled by the same se_look, applied by the same
# se_look_rows() that develops the JPEG. The sources live in film_sim/se so
# there is exactly one copy of the look code on the card.
#
# Objects are flat in $(BUILD_DIR) with an "se_" prefix so these rules win
# over the generic $(BUILD_DIR)/%.o: %.c (GNU make picks the shortest stem).
SE_ENGINE_SRC = se look stock lut wb color grade
SE_OBJS = $(addprefix $(BUILD_DIR)/se_,$(addsuffix .o,$(SE_ENGINE_SRC))) \
          $(BUILD_DIR)/se_libgcc.o

MODULE_OBJS = $(BUILD_DIR)/$(MODULE_NAME).o $(SE_OBJS)
TOP_DIR = ../..

include $(TOP_DIR)/modules/Makefile

# Vendored engine: silenced rather than patched (see film_sim/Makefile).
SE_ENGINE_CFLAGS = -Wno-double-promotion
SE_ENGINE_DIR = ../film_sim/se

$(BUILD_DIR)/se_%.o: $(SE_ENGINE_DIR)/engine/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) $(SE_ENGINE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/se_libgcc.o: $(SE_ENGINE_DIR)/se_libgcc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CDEPS_FLAGS) -c -o $@ $<

.DEFAULT_GOAL := $(BUILD_DIR)/module_complete
