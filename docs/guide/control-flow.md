# Sinistra has several control structures:

## IF ... THEN ... \[ELSIF ...] \[ELSE ...] ENDIF ##

`IF condition THEN statements; ENDIF;`  
`IF condition THEN statements; ELSE statements; ENDIF;`  
`IF condition THEN statements; ELSIF condition THEN statements; ENDIF;`  
`IF condition THEN statements; ELSIF condition THEN statements; ELSE statements; ENDIF;`

## WHILE ... DO ... ENDWHILE (and DO ... WHILE) ##
`WHILE condition DO statements; ENDWHILE;`
`DO statements; WHILE condition;`

## FOREACH ... IN ... DO ... ENDFOR ##
`FOREACH @local IN expression DO statements; ENDFOR;` evaluates `expression`
once, then visits list elements in order. The iterator is initialized to `nil`
and remains an ordinary item-local after the loop, containing the last visited
element or `nil` when no iteration occurred. Non-list values skip the body
without changing `error`; `BREAK` exits and `CONTINUE` advances. Lists are
immutable, so rebinding the source local in the body does not alter the
captured iteration. Nested loops use three hidden locals per level and count
against the 255-local limit.

`DO` loops execute their body at least once, then repeat while the condition is
truthy. For example, `@a = 0; DO sys.log{@a}; @a++; WHILE @a < 5;` logs a
counter from zero through four. The loop itself has no result value.

## BREAK, CONTINUE and RETURN ##
`BREAK;` exits the nearest enclosing loop. `CONTINUE;` skips to the next
iteration of the nearest loop: in a `WHILE ... DO` loop it re-tests the
condition, while in a `DO ... WHILE` loop it proceeds to the trailing condition
before deciding whether to repeat. These statements cannot be used outside a
loop.

`RETURN;` can be used at any point to halt execution of the item and return
`nil`. `RETURN expression;` evaluates the expression once, halts immediately,
and returns that value. Falling off the end also returns `nil`; expression
statements never implicitly become an item's result.

## Lists and item references

List literals evaluate left-to-right; bare code items execute while `&` creates an unresolved item-reference value. Immutable `list.append`, `list.set`, `list.concat`, and `list.slice` preserve their inputs and return derived lists. Example: `@r = &player; @xs = list.append{#[1, 2], @r};`.  For much more information, have a look at [lists.md](lists.md).

