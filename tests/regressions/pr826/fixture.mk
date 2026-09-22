# Invoke with the configured native Makefile; never edit production build files.
PR826_DIR := $(abspath tests/regressions/pr826)
PR826_SOURCE ?= tar/write.c
PR826_OUT ?= /tmp/pr826-fixture
PR826_CC ?= $(CC)
PR826_MAIN := $(filter tar/tarsnap-bsdtar.$(OBJEXT) tar/bsdtar.$(OBJEXT),$(tarsnap_OBJECTS))
PR826_WRITE := $(filter tar/tarsnap-write.$(OBJEXT) tar/write.$(OBJEXT),$(tarsnap_OBJECTS))
PR826_WRAPS := malloc calloc free ccache_entry_lookup ccache_entry_free writetape_setcallback fileutil_open_noatime close archive_write_multitape_setmode network_select
.PHONY: pr826-fixture
pr826-fixture:
	@test "$(words $(PR826_MAIN))" = 1 && test "$(words $(PR826_WRITE))" = 1
	@mkdir -p "$(PR826_OUT)"
	objcopy --redefine-sym main=pr826_unused_native_main $(PR826_MAIN) "$(PR826_OUT)/native-main.o"
	$(PR826_CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(tarsnap_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -O1 -g -DENTRY_SOURCE='"$(abspath $(PR826_SOURCE))"' -c "$(PR826_DIR)/entry.c" -o "$(PR826_OUT)/entry.o"
	$(PR826_CC) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) $(foreach symbol,$(PR826_WRAPS),-Wl$(comma)--wrap=$(symbol)) -o "$(PR826_OUT)/entry" "$(PR826_OUT)/entry.o" "$(PR826_OUT)/native-main.o" $(filter-out $(PR826_MAIN) $(PR826_WRITE),$(tarsnap_OBJECTS)) $(tarsnap_LDADD) $(LIBS)
comma := ,
