unexport O_TARGET
unexport O_OBJS
unexport L_TARGET
unexport L_OBJS
unexport EXTRA_CFLAGS
unexport EXTRA_ASFLAGS
unexport EXTRA_LDFLAGS
unexport EXTRA_ARFLAGS
unexport SUB_DIRS

.PHONY: first_rule all_targets sub_dirs

first_rule: sub_dirs
	$(Q)$(MAKE) all_targets

all_targets: $(O_TARGET) $(L_TARGET)

# --------------------------------
# common rules
# --------------------------------

%.o: %.c
	@echo -e "CC $@"
	$(Q)$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $($*_CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

%.o: %.S
	@echo -e "AS $@"
	$(Q)$(CC) -x assembler-with-cpp \
		$(ASFLAGS) $(EXTRA_ASFLAGS) $($*_ASFLAGS) $(ASPPFLAGS) -MMD -MP -c $< -o $@

# --------------------------------
# compile multiple .o files into a single .o/.a file
# --------------------------------

ifdef O_TARGET
$(O_TARGET): $(O_OBJS)
	$(Q)rm -f $@
ifneq ($(strip $^),"")
	@echo -e "LD $@"
	$(Q)$(LD) $(EXTRA_LDFLAGS) -r -o $@ $^
else
	@echo -e "LD $@ (empty)"
	$(Q)$(AR) rcs $@
endif
endif

ifdef L_TARGET
$(L_TARGET): $(L_OBJS)
	@echo -e "AR $@"
	$(Q)rm -f $@
	$(Q)$(AR) $(EXTRA_ARFLAGS) rcs $@ $^
endif

# --------------------------------
# build sibdirectories
# --------------------------------

ifdef SUB_DIRS
sub_dirs:
	$(Q)set -e; for i in $(SUB_DIRS); do $(MAKE) -C $$i; done
else
sub_dirs:
endif

# --------------------------------
# Include dependency files
# --------------------------------

-include $(O_OBJS:.o=.d)
-include $(L_OBJS:.o=.d)

