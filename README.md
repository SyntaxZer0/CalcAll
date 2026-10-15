# Calc

A calculator in C that keeps numbers exact. Whole numbers show every digit
(no `E+10` unless you ask for it), fractions stay fractions, and sums and
products (Σ and Π) are built in.

```
> 2^100
1267650600228229401496703205376
> prod(k=1, 10, k)
3628800
> sum(k=1, 100, k^2)
338350
> 0.1 + 0.2
0.3
> 1/3
0.33333333333333333333...  (= 1/3)
> :sci
scientific notation on (20 significant digits)
> 2^100
1.2676506002282294014...e+30
```

## Build and run

```sh
make            # builds ./calc
./calc          # interactive prompt
./calc '50!'    # evaluate once
make test       # run the tests
```

## What it understands

| | |
|---|---|
| Operators | `+ - * / % ^ !` and parentheses |
| Numbers | `42`, `3.14`, `.5`, `6.02e23`, all stored exactly |
| Sum | `sum(k=1, 100, k^2)` or `Σ(k=1, 100, k^2)` |
| Product | `prod(k=1, 10, k)` or `Π(k=1, 10, k)` |
| Functions | `abs floor ceil gcd lcm choose(n,k) perm(n,k)` |
| Variables | `ans` holds the previous result |
| Commands | `:sci`, `:fix`, `:digits N`, `:help`, `:quit` |

Not supported yet: non-integer powers, roots, trig, and logarithms. These
produce irrational numbers, so they need an approximate number type next to
the exact one.

## How it's built

```
src/bigint.c    arbitrary-precision integers (add, multiply, long division)
src/rational.c  exact fractions on top of bigint, plus number formatting
src/parser.c    text -> expression tree (recursive descent)
src/eval.c      walks the tree; sum/prod re-evaluate their body per index
src/calc.c      the engine API a front end calls: calc_eval(line) -> text
src/main.c      the terminal front end
```

A GUI is a new front end that calls `calc_eval` from `calc.h`, just as
`main.c` does.
