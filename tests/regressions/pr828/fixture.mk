# Invoke with the configured native Makefile; never edit production build files.
PR828_DIR := $(abspath tests/regressions/pr828)
PR828_SOURCE ?= tar/write.c
PR828_OUT ?= /tmp/pr828-fixture
PR828_CC ?= $(CC)
PR828_MAIN := $(filter tar/tarsnap-bsdtar.$(OBJEXT) tar/bsdtar.$(OBJEXT),$(tarsnap_OBJECTS))
PR828_WRITE := $(filter tar/tarsnap-write.$(OBJEXT) tar/write.$(OBJEXT),$(tarsnap_OBJECTS))
PR828_WRAPS := archive_read_new archive_read_finish archive_error_string bsdtar_warnc archive_read_open_multitape network_select
.PHONY: pr828-fixture
pr828-fixture:
	@test "$(words $(PR828_MAIN))" = 1 && test "$(words $(PR828_WRITE))" = 1
	@mkdir -p "$(PR828_OUT)"
	objcopy --redefine-sym main=pr828_unused_native_main $(PR828_MAIN) "$(PR828_OUT)/native-main.o"
	$(PR828_CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(tarsnap_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -O1 -g -DAPPEND_SOURCE='"$(abspath $(PR828_SOURCE))"' -c "$(PR828_DIR)/append.c" -o "$(PR828_OUT)/append.o"
	$(PR828_CC) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) $(foreach symbol,$(PR828_WRAPS),-Wl$(comma)--wrap=$(symbol)) -o "$(PR828_OUT)/append" "$(PR828_OUT)/append.o" "$(PR828_OUT)/native-main.o" $(filter-out $(PR828_MAIN) $(PR828_WRITE),$(tarsnap_OBJECTS)) $(tarsnap_LDADD) $(LIBS)
comma := ,
