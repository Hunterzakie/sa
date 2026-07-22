#! /bin/sh

test_name=${0##*/}
case "$test_name" in
    ?*--?*)
        program_name=${test_name%%--*}
        scenario=${test_name#*--}
        ;;
    *)
        printf '%s\n' "$test_name: expected PROGRAM--SCENARIO" >&2
        exit 1
        ;;
esac

test_dir=${0%/*}
if test "$test_dir" = "$0"; then
    test_dir=.
fi

program="$test_dir/$program_name"
if test ! -x "$program" && test -x "$program.exe"; then
    program="$program.exe"
fi

exec "$program" "$scenario" "$@"
