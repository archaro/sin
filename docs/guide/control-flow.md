# Control Flow in Code Items

Conditions use Sinistra's normal truthiness rules; see the documentation on
[expressions and values](expressions-and-values.md) for details.

Sinistra has several control structures:

## IF ... THEN ... \[ELSIF ...] \[ELSE ...] ENDIF
- `if condition then statements; endif;`  
- `if condition then statements; else statements; endif;`  
- `if condition then statements; elsif condition then statements; endif;`  
- `if condition then statements; elsif condition then statements; else statements; endif;`

## WHILE ... DO ... ENDWHILE (and DO ... WHILE)
- `while condition do statements; endwhile;`
- `do statements; while condition;`

`DO` loops execute their body at least once, then repeat while the condition is
truthy. For example, `@a = 0; DO sys.log{@a}; @a++; WHILE @a < 5;` logs a
counter from zero through four. The loop itself has no result value.

## FOREACH ... IN ... DO ... ENDFOR
- `foreach @local in expression do statements; endfor;`

The controlling `expression` is evaluated once. If it produces a
[list](lists.md) `foreach` then visits list elements in order. The iterator is
initialized to `nil` and remains an ordinary item-local after the loop,
containing the last visited element or `nil` when no iteration occurred.
`break` exits the loop and `continue` advances. Lists are immutable, so
rebinding the source local in the body does not alter the captured iteration.

## BREAK, CONTINUE and RETURN
- `break;` exits the nearest enclosing loop.
- `continue;` skips to the next iteration of the nearest loop: in a
`while ... do` loop it re-tests the condition, while in a `do ... while` loop
it proceeds to the trailing condition before deciding whether to repeat. These
statements cannot be used outside a loop.
- `return;` can be used at any point to halt execution of the item and return
`nil`. `return expression;` evaluates the expression once, halts immediately,
and returns that value. Falling off the end also returns `nil`; expression
statements never implicitly become an item's result.
