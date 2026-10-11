#!/bin/sh
# End-to-end smoke test: feeds scripted keystrokes to the program and checks
# the output. Runs in a temp directory so your real bookings.txt is untouched.

BIN="$(cd "$(dirname "$0")/.." && pwd)/railway"
WORK="$(mktemp -d)"
cd "$WORK" || exit 1
FAILS=0

check() {   # check <description> <file> <text that must appear>
    if grep -qF -- "$3" "$2"; then
        echo "PASS: $1"
    else
        echo "FAIL: $1   (expected: $3)"
        FAILS=$((FAILS + 1))
    fi
}

check_absent() {   # check_absent <description> <file> <text that must NOT appear>
    if grep -qF -- "$3" "$2"; then
        echo "FAIL: $1   (unexpected: $3)"
        FAILS=$((FAILS + 1))
    else
        echo "PASS: $1"
    fi
}

# ---- Run 1: book, double-book, auto-assign, query, cancel ---------------
printf '%s\n' \
  abc 1 1 5 "Asha Verma" 25 F y \
  1 1 5 0 "Ravi Kumar" 30 M y \
  2 1 \
  3 1 1000000001 \
  3 2 1 \
  4 1000000002 y \
  3 1 1000000002 \
  0 | "$BIN" > run1.txt

check "rejects non-numeric menu input"      run1.txt "Please enter a number between 0 and 5."
check "first booking gets PNR 1000000001"   run1.txt "PNR          : 1000000001"
check "booked seat shows as XX"             run1.txt "[XX]"
check "double booking is refused"           run1.txt "Seat 5 is already booked"
check "auto-assign picks seat 1"            run1.txt "Seat No.     : 1"
check "second booking gets PNR 1000000002"  run1.txt "PNR          : 1000000002"
check "PNR lookup shows passenger"          run1.txt "Asha Verma"
check "train listing shows both passengers" run1.txt "Total passengers: 2"
check "cancel gives 80% refund"             run1.txt "Refund of Rs. 1960"
check "cancelled PNR is gone"               run1.txt "No booking found for PNR 1000000002."

# ---- Run 2: data survives a restart; PNRs are never reused --------------
printf '%s\n' \
  3 1 1000000001 \
  3 1 1000000002 \
  1 1 0 "Meera Nair" 41 F y \
  0 | "$BIN" > run2.txt

check       "booking persisted after restart"   run2.txt "Loaded 1 existing booking(s)."
check       "persisted passenger found"          run2.txt "Asha Verma"
check       "cancelled booking stays cancelled"  run2.txt "No booking found for PNR 1000000002."
check       "cancelled PNR not reused"           run2.txt "PNR          : 1000000003"

# ---- Run 3: declined confirmation + EOF handling ------------------------
printf '%s\n' 1 2 0 "Test User" 20 O n 0 | "$BIN" > run3.txt
check "declining a booking saves nothing" run3.txt "Booking cancelled. Nothing was saved."

"$BIN" < /dev/null > run4.txt
check "exits cleanly on EOF" run4.txt "Goodbye."

# ---- Name validation ----------------------------------------------------
printf '%s\n' 1 3 0 "R2D2" "Valid Name" 22 M n 0 | "$BIN" > run5.txt
check "rejects names with digits" run5.txt "Enter a valid name"

cd / && rm -rf "$WORK"
if [ "$FAILS" -eq 0 ]; then
    echo "All tests passed."
else
    echo "$FAILS test(s) failed."
    exit 1
fi
