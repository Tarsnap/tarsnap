#!/bin/sh
#
# tarsnap-stats.sh - summary of tarsnap statistics and storage cost
#
# Sizes are printed with SI prefixes (kB/MB/GB = 10^3) by default, matching
# "tarsnap --humanize-numbers --print-stats" exactly: tarsnap(1) states that
# --humanize-numbers uses "SI prefixes". Pass -b for binary prefixes
# (KiB/MiB/GiB = 2^10), correctly labelled.
#
# Network behaviour: --print-stats is computed locally from the cache
# directory ("Global statistics are calculated based on the current cache
# directory, without [...] querying the Tarsnap servers" -- tarsnap(1)), so
# the only network call made here is --list-archives, needed solely to count
# the snapshots. --no-aggressive-networking is passed on every invocation so
# the script never opens multiple TCP connections to the tarsnap servers,
# even if aggressive-networking is set in a configuration file.
#
# Key file: by default tarsnap reads its own configuration files, where the
# key path is usually declared as "keyfile /root/tarsnap.key", so nothing has
# to be passed here. Use -k (or the TARSNAP_KEYFILE environment variable) when
# the key of THIS machine lives somewhere else. It is not a way to report on
# another machine's backups: --print-stats never queries the server, it reads
# the local cache directory, so a foreign key would list that machine's
# archives while printing this machine's sizes.
#
# Do NOT put this script in a crontab. Run it by hand, when you actually want
# to look at the numbers. Reasons, in decreasing order of certainty:
#   - --list-archives downloads and decrypts the whole archive metadata on
#     every single run; tarsnap bills bandwidth per byte transmitted, so
#     polling it costs money for figures that only change when an archive is
#     created or deleted.
#   - --print-stats reads the cache directory while a scheduled backup may be
#     writing to it, which can yield an inconsistent snapshot.
#   - a passphrase-protected key file cannot be unlocked without a terminal,
#     so the script simply fails under cron.
#   - with -k/-c, the borrowed cache directory drifts out of sync and would
#     need a --fsck, which is a heavy network operation.
# If you want the numbers automatically, call tarsnap with --print-stats from
# your backup script instead: it costs nothing extra there.
#
# Order of operations matters: the configuration file is validated first,
# because that check needs no key at all. Only then do the two key-using
# calls run, so a passphrase is never requested for a run that was going to
# abort anyway. Note that each of those two calls loads the key file
# independently, so a passphrase-protected key is prompted for twice.
#
# Progress is reported on stderr only, and only when stderr is a terminal:
# stdout stays clean so the output can be piped or redirected as-is. While the
# archive list is being fetched, the live count and the name of the last
# archive received are shown; nothing at all is drawn before the first archive
# name arrives, so the progress line can never land on top of a passphrase
# prompt. That live mode needs stdbuf(1); without it (BSD, macOS) the script
# falls back to a plain spinner, which does start drawing immediately and may
# therefore overlap a passphrase prompt. Set TARSNAP_SPINNER=0 to disable it.
#
# Exit status:
#   0  success
#   2  command line usage error
#   3  configuration file rejected by "tarsnap --verify-config"
#   4  "tarsnap --list-archives" failed
#   5  "tarsnap --print-stats" failed
#   6  unrecognised "tarsnap --print-stats" output
#   7  could not create a temporary file
#   8  -k points at an unreadable key file

# -e: abort on the first unhandled failure rather than carrying on with bad
#     data; every call whose failure we want to inspect ourselves is written
#     as "cmd || rc=$?", which is exempt from -e.
# -u: a typo in a variable name becomes a hard error instead of an empty
#     string silently propagating into the output.
# "pipefail" is deliberately NOT used: it is not POSIX (dash rejects it, and
#     /bin/sh is dash on Debian), and nothing here relies on the exit status
#     of a pipeline anyway.
set -eu

# Force '.' as the decimal separator regardless of the machine locale:
# without this, awk prints "2,1417" under fr_FR and the output is no longer
# parsable by other tools.
LC_ALL=C
export LC_ALL

TARSNAP=${TARSNAP:-tarsnap}       # path to the binary, NOT a command with arguments
KEYFILE=${TARSNAP_KEYFILE:-}      # empty: let tarsnap.conf decide
TSOPT='--no-aggressive-networking'
BASE=1000                       # 1000 = SI (default, matches tarsnap), 1024 = binary

usage() {
    cat <<EOF
usage: $0 [-b] [-k keyfile]
  -b           use binary prefixes (KiB/MiB/GiB) instead of SI ones
  -k keyfile   key file of this machine, when it is not where tarsnap.conf
               says it is (default there is usually /root/tarsnap.key)
EOF
}

# POSIX getopts knows nothing about long options: it would treat --help as a
# malformed short option and complain on stderr before usage() ever runs.
# --help is what everyone types first, so it is handled before the loop.
case "${1:-}" in
    --help) usage; exit 0 ;;
esac

# The leading ':' selects getopts' silent mode: the shell keeps its own
# "Illegal option" message to itself and we report the problem ourselves, so
# the wording is the same whichever shell runs the script.
while getopts ':bk:h' opt; do
    case "$opt" in
        b) BASE=1024 ;;
        k) KEYFILE=$OPTARG ;;
        h) usage; exit 0 ;;
        :) printf '%s: option -%s requires an argument\n' "$0" "$OPTARG" >&2
           usage >&2; exit 2 ;;
        *) printf '%s: unknown option -%s\n' "$0" "$OPTARG" >&2
           usage >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

# Nothing but options is expected: a leftover operand is almost always a typo
# (a key path given without -k, for instance), and silently ignoring it would
# print statistics for the wrong thing.
if [ "$#" -gt 0 ]; then
    printf '%s: unexpected argument: %s\n' "$0" "$1" >&2
    usage >&2
    exit 2
fi

# "tarsnap --verify-config" validates the configuration syntax, it does not
# look at the key file at all, so check it here to fail with a useful message
# rather than with tarsnap's "Cannot read key file" three calls later.
if [ -n "$KEYFILE" ] && [ ! -r "$KEYFILE" ]; then
    printf '%s: key file not readable: %s\n' "$0" "$KEYFILE" >&2
    exit 8
fi
# Build the option list once, then reuse it for every invocation. The key path
# has to be a separate argv element, which is why it gets its own variable
# instead of being folded into $TARSNAP: "$TARSNAP" is quoted, so a value like
# "tarsnap --keyfile /path" would be looked up as one single command name.
# Verified against tarsnap 1.0.41: --keyfile is accepted by --verify-config,
# --list-archives and --print-stats alike.
set -- "$TSOPT"
if [ -n "$KEYFILE" ]; then
    set -- "$@" --keyfile "$KEYFILE"
fi

# --- Temporary file and cleanup ----------------------------------------------
# The archive list is written to a file rather than captured in $(...) so that
# the progress reporter can watch it grow.
tmpout=''
spinner_pid=''

cleanup() {
    if [ -n "$spinner_pid" ]; then
        kill "$spinner_pid" 2>/dev/null || true
        wait "$spinner_pid" 2>/dev/null || true
        spinner_pid=''
        # Blank the line with a fixed-width field: no ANSI escape needed, so
        # this also behaves on a dumb terminal.
        printf '\r%78s\r' '' >&2
    fi
    [ -n "$tmpout" ] && rm -f "$tmpout"
    return 0
}

# The progress line and the temporary file must never outlive the script.
# cleanup() is idempotent, so the INT/TERM handlers re-entering it via the
# EXIT trap is harmless.
trap 'cleanup' EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM

tmpout=$(mktemp "${TMPDIR:-/tmp}/tarsnap-stats.XXXXXX") || {
    printf '%s: could not create a temporary file in %s\n' "$0" "${TMPDIR:-/tmp}" >&2
    exit 7
}

# stdbuf(1) forces tarsnap to flush every line instead of filling a 4 kB stdio
# buffer first; without it the archive count would jump from 0 to the total in
# one go. It is a GNU coreutils tool, absent on the BSDs and on macOS, hence
# the graceful fallback to a plain spinner.
if command -v stdbuf >/dev/null 2>&1; then
    STDBUF='stdbuf'
    SPIN_MODE='count'       # live count of archives received
else
    STDBUF=''
    SPIN_MODE='plain'       # nothing to count, fall back to a bare spinner
fi

# --- Progress reporting ------------------------------------------------------
# $1: label used until the first line of output shows up (plain spinner mode)
# $2: "count" to report live progress from $tmpout, anything else for a plain
#     spinner.
spin_start() {
    [ "${TARSNAP_SPINNER:-1}" = 1 ] || return 0
    [ -t 2 ] || return 0
    _label=$1
    _mode=$2
    {
        while :; do
            for frame in '|' '/' '-' "\\"; do
                if [ "$_mode" = count ]; then
                    # Strictly nothing is drawn until the first archive name
                    # has arrived: during that window tarsnap may still be
                    # asking for the key file passphrase, and the progress
                    # line must not land on top of the prompt.
                    if [ -s "$tmpout" ]; then
                        # Live count and most recently received archive name.
                        # The name is truncated to keep the line under 78
                        # columns.
                        _n=$(grep -c . "$tmpout" 2>/dev/null || echo 0)
                        _last=$(tail -n 1 "$tmpout" 2>/dev/null || true)
                        [ "$_n" -eq 1 ] && _word=archive || _word=archives
                        printf '\r%s %d %s, last: %.40s' "$frame" "$_n" "$_word" "$_last" >&2
                    fi
                else
                    printf '\r%s %s' "$frame" "$_label" >&2
                fi
                # Fractional sleep is not POSIX, but every sleep(1) that
                # matters here (GNU, FreeBSD, macOS, busybox) accepts it.
                sleep 0.2
            done
        done
    } &
    spinner_pid=$!
}

spin_stop() {
    [ -n "$spinner_pid" ] || return 0
    kill "$spinner_pid" 2>/dev/null || true
    wait "$spinner_pid" 2>/dev/null || true
    spinner_pid=''
    printf '\r%78s\r' '' >&2
}

# --- Pre-flight check --------------------------------------------------------
# A broken tarsnap.conf makes every later call fail in a confusing way, so
# validate it first. This is purely local: no key, no cache, no network, and
# therefore no passphrase prompt.
# tarsnap prints the offending line on stderr by itself, we only add context.
rc=0
"$TARSNAP" "$@" --verify-config || rc=$?
if [ "$rc" -ne 0 ]; then
    printf '%s: "tarsnap --verify-config" rejected the configuration (exit status %d); aborting before any key is read or any network call is made\n' \
        "$0" "$rc" >&2
    exit 3
fi

# --- Data collection ---------------------------------------------------------
# Both calls are checked individually: inside a pipeline tarsnap's exit status
# would be hidden by wc/awk, and a failure (missing key, network down,
# out-of-sync cache directory) would silently produce wrong statistics.

rc=0
spin_start 'contacting the tarsnap server' "$SPIN_MODE"
if [ -n "$STDBUF" ]; then
    "$STDBUF" -oL "$TARSNAP" "$@" --list-archives > "$tmpout" || rc=$?
else
    "$TARSNAP" "$@" --list-archives > "$tmpout" || rc=$?
fi
spin_stop
if [ "$rc" -ne 0 ]; then
    printf '%s: "tarsnap --list-archives" failed with exit status %d; snapshot count unavailable\n' \
        "$0" "$rc" >&2
    exit 4
fi
# "grep -c ." ignores any trailing empty line; "|| true" keeps grep's exit
# status 1 (no match, i.e. no archive at all) from tripping set -e.
narchives=$(grep -c . "$tmpout" || true)

# No progress line here on purpose: --print-stats is a local cache directory
# read, it produces no incremental output to count, and a spinner would be the
# one thing able to overwrite a passphrase prompt.
rc=0
stats=$("$TARSNAP" "$@" --print-stats) || rc=$?
if [ "$rc" -ne 0 ]; then
    printf '%s: "tarsnap --print-stats" failed with exit status %d; check --cachedir and --keyfile\n' \
        "$0" "$rc" >&2
    exit 5
fi

# --- Formatting --------------------------------------------------------------
rc=0
printf '%s\n' "$stats" | awk \
    -v narchives="$narchives" \
    -v base="$BASE" \
    -v year="$(date +%Y)" \
    -v month="$(date +%m)" '
function format_bytes(bytes,   unit, i) {
    # One suffix table, picked according to the requested base.
    if (base == 1024)
        split("B KiB MiB GiB TiB PiB", unit, " ")
    else
        split("B kB MB GB TB PB", unit, " ")
    i = 1
    while (bytes >= base && i < 6) {
        bytes /= base
        i++
    }
    if (i == 1)
        return sprintf("%d B", bytes)
    return sprintf("%.2f %s", bytes, unit[i])
}

# Percentage guarded against division by zero (fresh account, newly
# initialised cache directory).
function ratio(num, den) {
    if (den <= 0)
        return "n/a"
    return sprintf("%.1f%%", (num / den) * 100)
}

# Tarsnap bills per byte-day, so the daily cost depends on the actual length
# of the current month, not on a flat 30 (see the tarsnap FAQ, "Why did my
# daily storage usage cost change...").
function days_in_month(y, m,   d) {
    split("31 28 31 30 31 30 31 31 30 31 30 31", d, " ")
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
        return 29
    return d[m]
}

# Match on patterns rather than line numbers, and read the two LAST fields:
# immune both to a warning line appearing above the table and to a change in
# the number of words in the row label.
/^All archives/ {
    all_total = $(NF - 1); all_comp = $NF; got_all = 1
}
/\(unique data\)/ {
    uniq_total = $(NF - 1); uniq_comp = $NF; got_uniq = 1
}

END {
    if (!got_all || !got_uniq) {
        print "unrecognised --print-stats output: no \"All archives\" and/or \"(unique data)\" row found" > "/dev/stderr"
        exit 1
    }

    printf "All archives:\n"
    printf "  Snapshots:        %d\n",  narchives + 0
    printf "  Total size:       %s\n",  format_bytes(all_total)
    printf "  Compressed size:  %s\n",  format_bytes(all_comp)
    printf "  Ratio:            %s\n\n", ratio(all_comp, all_total)

    printf "Unique data:\n"
    printf "  Total size:       %s\n",  format_bytes(uniq_total)
    printf "  Encoded size:     %s\n",  format_bytes(uniq_comp)
    printf "  Ratio:            %s\n\n", ratio(uniq_comp, uniq_total)

    # 250 picodollars per byte-month of encoded data ($0.25 / GB-month, with
    # GB = 10^9), as advertised on the tarsnap.com front page.
    monthly = uniq_comp * 250 / 1e12
    ndays   = days_in_month(year + 0, month + 0)

    printf "Storage cost:\n"
    printf "  Monthly:          $%.4f\n", monthly
    printf "  Daily:            $%.6f (%d-day month)\n", monthly / ndays, ndays
    printf "  (250 pUSD/byte/month of encoded data)\n"
}
' || rc=$?
if [ "$rc" -ne 0 ]; then
    printf '%s: could not parse the "tarsnap --print-stats" table\n' "$0" >&2
    exit 6
fi
