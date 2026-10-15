# Calc

![CalcAll](assets/logo/wordmark.png)

A calculator in C that keeps numbers exact. Whole numbers show every digit
(no `E+10` unless you ask for it), fractions stay fractions, and sums and
products (Σ and Π) are built in. Roots, logs and trig give an exact answer
when one exists (`sqrt(16)` = 4, `log(1000)` = 3) and a correctly rounded
approximation, marked `≈`, when it doesn't.

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
> sqrt(2)
≈ 1.4142135623730950488
> 8^(2/3)
4
> :deg
angles in degrees
> sin(30)
0.5
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

### GUI

The GUI uses [raylib](https://www.raylib.com). On macOS:

```sh
brew install raylib
make gui
./calc-gui
```

The screen works like a Casio "natural display": the expression is drawn the
way it looks on paper, and the answer appears in the bottom right.

- The fraction, root, power, log and |x| buttons insert templates with boxes to
  fill in. Σ and Π put the upper limit on top and `k=` the start below.
- Arrow keys move between boxes; Up/Down move between the top and bottom of a
  fraction or sum, and otherwise replay earlier calculations.
- On the keyboard, `/` turns the number before it into a numerator and `^`
  starts a power. Enter evaluates, Esc clears, Cmd+C copies the result.
- Exact answers that aren't whole numbers show as fractions; S⇔D switches
  them to decimals.
- Starting with an operator continues from the last answer (`×3` becomes
  `Ans×3`).
- Tab and Shift+Tab move to the next and previous box.

The app starts in **TEXT** mode, for typing everything; the **KEYS/TEXT**
switch brings the buttons back, and the app remembers which you used last
(along with RAD/DEG, FIX/SCI, S⇔D and the number of digits). The **History**
button on the Calculate screen lists past calculations, drawn the same way;
click one to bring it back and work it out again. In TEXT
mode the calculator syntax from the terminal version turns into templates as
you type: `sum` becomes Σ, and so do `prod`, `sqrt`, `root`, `cbrt`, `abs`,
`exp`, `frac` and `pi`. Inside a template, `,` (or `=` after a sum's
variable) moves to the next box and `)` leaves it, so typing or pasting
`sum(k=1, 10, k^2)` builds the Σ with its boxes filled in. `1/2+3` and
`2^10-1` follow normal precedence, ending the fraction or power at the
operator.

The **Table** tab works like a Casio's TABLE mode: enter f(x) and, if you
like, g(x), then Start, End and Step (expressions such as `pi/4` are fine),
and press Enter on Step or the Table button. Tables can have up to 1000 rows;
x goes up exactly, so a step of 0.1 never drifts. Arrow keys or clicks select
a cell, and its full-precision value shows at the bottom (Copy copies it).
To jump through a long table, just type a row number (or `x=2.5` for the row
whose x is closest) into the Go to box and press Enter.
Cells that can't be computed, like 1/x at 0, show ERROR. In the terminal,
`:table x^2 + 1 from 1 to 5 step 1` prints the same thing (add `; g(x)` after
f(x) for a second column).

The **Graph** tab works like Desmos. Type one line per expression: `y = x^2`,
`sin(x)` or `f(x) = ...` draws a curve in its own color, `a = 2` makes a
slider, and a line that uses an undefined name like `y = ax + 1` offers an
"add slider" button for it. Drag to pan, scroll to zoom (the buttons at the
top right zoom too, and ⌂ goes back to the start), and hover over a curve to
trace it. Click a curve to show dots at its roots, turning points,
y-intercept and crossings with other curves; click a dot and the exact engine
pins it down to full precision, exactly when it can (the minimum of
x^2 - 2x + 3 is exactly (1, 2); a root of x^2 - 2 is √2 to every digit). The
window widens the first time you open the Graph tab, and the list sits beside
the graph when there's room.

Lines can also be:

| | |
|---|---|
| Equations | `x^2 + y^2 = 9`, `xy = 1`, `x = y^2 - 4`, `y = y^2 + x` |
| Inequalities | `y < sin(x)`, `x^2 + y^2 <= 4`, `x > 4` (shaded; a dashed edge isn't included) |
| Parametric curves | `(3cos(t), 2sin(2t))`, with t's range in the boxes under the line |
| Polar curves | `r = 1 + cos(θ)` (type `theta`), with θ's range under the line |
| Points | `(1, 2)`, `(1, 2), (-4, 1), (a, -3)`; click one for its exact coordinates |

`<=` and `>=` can be typed for ≤ and ≥. As in Desmos, letters next to each
other multiply (`xy = 1` is a hyperbola) and `r = 2` is a circle, not a
slider.

Curves are drawn with a fast floating-point evaluator so panning stays
smooth; asymptotes (1/x, tan x) and jumps (floor x) are left as gaps instead
of being joined up. Graph lines can use your own variables and functions,
and their own definitions stay local to the graph.

On macOS, `make app` builds **CalcAll.app**, which has the logo as its Dock
icon; open it with `open CalcAll.app` or drag it into Applications.

Run it from the repository folder, or keep the `assets` folder next to
`calc-gui`, so it can find its font.

## What it understands

| | |
|---|---|
| Operators | `+ - * / % ^ !` and parentheses |
| Numbers | `42`, `3.14`, `.5`, `6.02e23`, all stored exactly |
| Constants | `pi` (or `π`), `e` |
| Roots | `sqrt(x)`, `cbrt(x)`, `root(x, n)`, `x^(p/q)` |
| Logs | `ln(x)`, `log(x)` (base 10), `log2(x)`, `log(x, base)`, `exp(x)` |
| Trig | `sin cos tan asin acos atan sinh cosh tanh` |
| Complex | `i`, `3+4i`, `re im conj arg abs` |
| Sum | `sum(k=1, 100, k^2)` or `Σ(k=1, 100, k^2)` |
| Product | `prod(k=1, 10, k)` or `Π(k=1, 10, k)` |
| Other | `abs floor ceil round gcd lcm choose(n,k) perm(n,k)` |
| Variables | `x = 5`, `rate = 0.05`, `θ = pi/3`; `ans` (or `Ans`) is the previous result |
| Functions | `f(x) = x^2 + 1`, `g(a, b) = sqrt(a^2 + b^2)`, then `f(3)`, `g(3, 4)` |
| Implicit × | `2pi`, `3(4+1)`, `2sin(30)` multiply |
| Commands | `:deg`, `:rad`, `:sci`, `:fix`, `:digits N`, `:help`, `:quit` |
| Definitions | `:vars`, `:clear [name]`, `:save [file]`, `:load file` |
| History | `:history [N]` lists past calculations, `:history clear` forgets them |
| Tables | `:table x^2 + 1 from 1 to 5 step 1`, `:table sin(x); cos(x) from 0 to pi step pi/4` |

Approximate results are computed with 10 more digits than are shown, so
every digit you see is correct. `:digits 1000` gives π to 1000 places.

A variable stores the value its right side has when you define it
(`y = x^2` keeps 25 even if `x` changes later). A function's body is looked
up each time it runs, so it uses the current values of your variables.
Built-in names like `sin`, `pi` and `ans` can't be redefined. Letters next to
each other multiply when there's no variable by that name, so with `x` and
`y` defined, `xy` is `x*y`.

Definitions are saved automatically to `~/.calc_definitions` after every
change and loaded again at startup, in the terminal and the GUI alike. Values
are stored as exact fractions, so `x = 1/3` comes back as exactly 1/3 and an
approximate value keeps all its digits. Set `CALC_DEFINITIONS` to use another
file, or to an empty value to turn saving off. `:save file` and `:load file`
keep separate sets.

Every calculation is also kept in `~/.calc_history` (the last 500), so
`:history` and the GUI's History button show them in a later session too. Set
`CALC_HISTORY` to use another file, or to an empty value to keep none.

Complex numbers work everywhere a real number does, and stay exact when
they can: `(1+2i)(3-i)` = `5 + 5i`, `sqrt(-4)` = `2i`, `sqrt(3+4i)` = `2 + i`,
`abs(3+4i)` = `5`, while `e^(iπ)` gives `≈ -1` and `asin(2)` gives
`≈ 1.5707963267948966192 - 1.3169578969248167086i`. Functions use their
principal values, like most calculators; odd roots of negative numbers stay
real, so `cbrt(-8)` and `(-8)^(1/3)` are `-2`.

## How it's built

```
src/bigint.c    arbitrary-precision integers (add, multiply, long division)
src/rational.c  exact fractions on top of bigint, plus number formatting
src/approx.c    sqrt, pi, exp, ln, sin, cos, atan via series in fixed point
src/cmath.c     complex arithmetic and complex exp, ln, sqrt, sin, cos, ...
src/parser.c    text -> expression tree (recursive descent)
src/eval.c      walks the tree; tracks exact vs approximate; all functions
src/symbols.c   the table of user variables and functions
src/feval.c     fast floating-point evaluation for drawing graphs
src/graph.c     the graph's expression list, and exact key points
src/calc.c      the engine API a front end calls: calc_eval(line) -> text
src/main.c      the terminal front end
src/mathedit.c  the natural-display editor: templates, cursor, to-text
src/gui.c       the raylib front end: draws the templates and buttons
```

Both front ends only use `calc.h`: they pass a line of text to `calc_eval`
and show the text it returns.

The bundled font is DejaVu Sans Mono; see `assets/DejaVu-LICENSE.txt`.
