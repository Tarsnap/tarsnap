# Use the configured native build; do not change its production Makefile.
PR824_DIR := $(abspath tests/regressions/pr824)
PR824_SOURCE ?= tar/util.c
PR824_OUT ?= /tmp/pr824-fixture
PR824_CC ?= $(CC)
PR824_EXTRA ?=
PR824_MAIN := $(filter tar/tarsnap-bsdtar.$(OBJEXT) tar/bsdtar.$(OBJEXT),$(tarsnap_OBJECTS))
PR824_UTIL := $(filter tar/tarsnap-util.$(OBJEXT) tar/util.$(OBJEXT),$(tarsnap_OBJECTS))
.PHONY: pr824-fixture
pr824-fixture:
	@test "$(words $(PR824_MAIN))" = 1 && test "$(words $(PR824_UTIL))" = 1
	@mkdir -p "$(PR824_OUT)"
	objcopy --redefine-sym main=pr824_unused_native_main $(PR824_MAIN) "$(PR824_OUT)/native-main.o"
	$(PR824_CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(tarsnap_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -O1 -g $(PR824_EXTRA) -DUTIL_SOURCE='"$(abspath $(PR824_SOURCE))"' -c "$(PR824_DIR)/pathname.c" -o "$(PR824_OUT)/pathname.o"
	$(PR824_CC) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) $(PR824_EXTRA) -Wl,--wrap=archive_entry_copy_pathname -Wl,--wrap=network_select -o "$(PR824_OUT)/pathname" "$(PR824_OUT)/pathname.o" "$(PR824_OUT)/native-main.o" $(filter-out $(PR824_MAIN) $(PR824_UTIL),$(tarsnap_OBJECTS)) $(tarsnap_LDADD) $(LIBS)
