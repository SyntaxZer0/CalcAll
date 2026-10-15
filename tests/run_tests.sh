#!/bin/sh
# Usage: tests/run_tests.sh [path/to/calc]
CALC=${1:-./calc}
pass=0
fail=0

report() {
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s\n  expected: %s\n  actual:   %s\n' "$1" "$2" "$3"
    fi
}

# check EXPR EXPECTED  -- evaluates one expression
check() {
    report "$1" "$2" "$("$CALC" "$1" 2>&1)"
}

# session EXPECTED LINE...  -- feeds lines to the REPL, compares the last output line
session() {
    expected=$1
    shift
    report "$*" "$expected" "$(printf '%s\n' "$@" | "$CALC" 2>&1 | tail -n 1)"
}

# Arithmetic and precedence
check '1 + 2 * 3' 7
check '(1 + 2) * 3' 9
check '-2^2' -4
check '2^3^2' 512
check '2^-3' 0.125
check '7 % 3' 1
check '-7 % 3' 2
check '10 - 4 - 3' 3
check '2 * -3' -6

# Exact numbers
check '0.1 + 0.2' 0.3
check '1/3' '0.33333333333333333333...  (= 1/3)'
check '1/3 * 3' 1
check '1.5e3' 1500
check '1e-3' 0.001
check '2^100' 1267650600228229401496703205376
check '30!' 265252859812191058636308480000000
check '25!/23!' 600

# Sums and products
check 'sum(k=1, 100, k)' 5050
check 'sum(k=1, 100, k^2)' 338350
check 'prod(k=1, 10, k)' 3628800
check 'prod(k=1, 0, k)' 1
check 'sum(k=5, 1, k)' 0
check 'sum(i=1, 3, sum(j=1, i, j))' 10
check 'prod(k=1, 4, (1 + 1/k))' 5
check 'Σ(k=1, 10, k)' 55
check 'Π(k=1, 5, k)' 120

# Functions
check 'abs(-5/2)' 2.5
check 'floor(-7/2)' -4
check 'ceil(7/2)' 4
check 'gcd(48, 180)' 12
check 'lcm(4, 6)' 12
check 'choose(52, 5)' 2598960
check 'perm(10, 3)' 720

# Errors
check '1/0' 'error: division by zero'
check '(1 + 2' "error: expected ')' but the input ended (at column 7)"
check 'x + 1' "error: unknown variable 'x'"
check 'foo(1)' "error: unknown function 'foo'"
check '(-1)!' 'error: factorial of a negative number is undefined'
check '2^0.5' 'error: non-integer exponents are not supported yet'
check '9^9^9' 'error: result would have about 369693100 digits (limit is 1000000)'

# REPL features
session 11 '5 + 5' 'ans + 1'
session '1.2676506002282294014...e+30' ':sci' '2^100'
session '-1.25e-4' ':sci' '-1/8000'
session '0.333...  (= 1/3)' ':digits 3' '1/3'

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
