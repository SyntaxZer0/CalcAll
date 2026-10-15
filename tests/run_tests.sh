#!/bin/sh
# Usage: tests/run_tests.sh [path/to/calc]
CALC=${1:-./calc}
# Never read or write the real ~/.calc_definitions; the saving tests below
# use a temporary file of their own.
export CALC_DEFINITIONS=
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

# Roots: exact when possible
check 'sqrt(16)' 4
check 'sqrt(9/4)' 1.5
check 'cbrt(-27)' -3
check 'root(32, 5)' 2
check '8^(2/3)' 4
check '(-8)^(1/3)' -2
check 'sqrt(2)' '≈ 1.4142135623730950488'
check '2^0.5' '≈ 1.4142135623730950488'
check 'sqrt(2)^2' '≈ 2'

# Constants, logs, exponentials
check 'pi' '≈ 3.1415926535897932385'
check 'e' '≈ 2.7182818284590452354'
check 'ln(2)' '≈ 0.69314718055994530942'
check 'log(1000)' 3
check 'log2(1/8)' -3
check 'log(81, 3)' 4
check 'ln(1)' 0
check 'exp(0)' 1
check 'e^pi' '≈ 23.140692632779269006'
check 'exp(1000)' '≈ 1.9700711140170469939e+434'

# Trig (radians by default)
check 'sin(1)' '≈ 0.84147098480789650665'
check 'sin(pi)' '≈ 0'
check 'atan(1) * 4' '≈ 3.1415926535897932385'
check 'acos(0.5)' '≈ 1.0471975511965977462'
check 'sinh(1)' '≈ 1.1752011936438014569'
check 'tanh(100)' '≈ 1'
check 'tan(pi/2)' 'error: tan is undefined here (cos is 0)'

# Rounding
check 'round(2.5)' 3
check 'round(-2.5)' -3

# Implicit multiplication
check '2(3+4)' 14
check '(1+2)(3+4)' 21
check '2pi' '≈ 6.2831853071795864769'
session 15 '5' '3Ans'

# Complex numbers: exact where possible
check 'i^2' -1
check '(1+2i)(3-i)' '5 + 5i'
check '1/(1+i)' '0.5 - 0.5i'
check 'sqrt(-4)' 2i
check 'sqrt(3+4i)' '2 + i'
check '(-4)^(1/2)' 2i
check '(-8)^(1/3)' -2
check '(1+i)^10' 32i
check 'abs(3+4i)' 5
check 'conj(2-3i)' '2 + 3i'
check 're(2-3i)' 2
check 'im(2-3i)' -3
check 'sum(k=0, 3, i^k)' 0
check 'sqrt(-2)' '≈ 1.4142135623730950488i'
check 'e^(i*pi)' '≈ -1'
check 'exp(i*pi/2)' '≈ i'
check 'ln(-1)' '≈ 3.1415926535897932385i'
check 'i^i' '≈ 0.20787957635076190855'
check 'asin(2)' '≈ 1.5707963267948966192 - 1.3169578969248167086i'
check 'arg(1+i)' '≈ 0.78539816339744830962'
session 45 ':deg' 'arg(1+i)'
session 90 ':deg' 'arg(i)'
check 'e^(iπ)' '≈ -1'
check 'ie' '≈ 2.7182818284590452354i'
check 'sum(k=1, 4, ki)' 10i
check 'floor(i)' 'error: floor needs real numbers'
check 'i!' 'error: factorial argument must be a real number'

# Errors
check '1/0' 'error: division by zero'
check '(1 + 2' "error: expected ')' but the input ended (at column 7)"
check 'x + 1' "error: unknown variable 'x'"
check 'foo(1)' "error: unknown function 'foo'"
check '(-1)!' 'error: factorial of a negative number is undefined'
check 'ln(0)' 'error: log of zero is undefined'
check '9^9^9' 'error: result would have about 369693100 digits (limit is 1000000)'

# Variables and functions
session 'x = 5' 'x = 5'
session 11 'x = 5' '2x + 1'
session 25 'x = 5' 'y = x^2' 'x = 7' 'y'
session '1628.89462677744140625' 'rate = 0.05' '1000(1+rate)^10'
session 'f(x) = x^2 + 1' 'f(x) = x^2 + 1'
session 10 'f(x) = x^2 + 1' 'f(3)'
session 0 'f(x) = x^2 + 1' 'f(i)'
session 5 'g(a, b) = sqrt(a^2 + b^2)' 'g(3, 4)'
session 17 'f(x) = x^2 + 1' 'h(n) = sum(k=1, n, f(k))' 'h(3)'
session 101 'f(t) = t^2 + x' 'x = 1' 'f(10)'
session 250 'x = 10' 'y = 25' 'xy'
session 'error: fact calls itself too many times' 'fact(n) = n fact(n-1)' 'fact(3)'
session "error: 'pi' is built in and can't be changed" 'pi = 3'
session "error: 'sin' is built in and can't be redefined" 'sin(x) = 2'
session "error: parameter 'a' appears twice" 'g(a, a) = a'
session 'error: f takes 1 argument' 'f(x) = x' 'f(1, 2)'
session "error: unknown variable 'y'" 'y = 2' ':clear y' 'y'
session 'removed 2 definitions' 'x = 1' 'f(x) = x' ':clear'

# REPL features
session 0.5 ':deg' 'sin(30)'
session 1 ':deg' 'tan(45)'
session 60 ':deg' 'acos(1/2)'
session '≈ 0.7071067811865475244' ':deg' 'sin(45)'
session 'error: tan is undefined at 90 degrees' ':deg' 'tan(90)'
session '≈ 3.1415926535897932384626433832795028841971693993751' ':digits 50' 'pi'
session 11 '5 + 5' 'ans + 1'
session '1.2676506002282294014...e+30' ':sci' '2^100'
session '-1.25e-4' ':sci' '-1/8000'
session '0.333...  (= 1/3)' ':digits 3' '1/3'

# Saving: definitions come back in a new run, exact or approximate as before
store=$(mktemp)
printf '%s\n' 'x = 1/3' 'θ = pi/3' 'z = 3-4i' 'f(t) = t + x' | CALC_DEFINITIONS=$store "$CALC" >/dev/null
report 'saved function' '1.33333333333333333333...  (= 4/3)' "$(CALC_DEFINITIONS=$store "$CALC" 'f(1)' 2>&1)"
report 'saved approx' '≈ 1.0471975511965977462' "$(CALC_DEFINITIONS=$store "$CALC" 'θ' 2>&1)"
report 'saved complex' '-7 - 24i' "$(CALC_DEFINITIONS=$store "$CALC" 'z^2' 2>&1)"
CALC_DEFINITIONS=$store "$CALC" ':clear x' >/dev/null
report 'saved clear' "error: unknown variable 'x'" "$(CALC_DEFINITIONS=$store "$CALC" 'x' 2>&1)"
CALC_DEFINITIONS=$store "$CALC" ":save $store.copy" >/dev/null
report 'load file' "loaded 3 definitions from $store.copy" "$("$CALC" ":load $store.copy" 2>&1)"
rm -f "$store" "$store.copy"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
