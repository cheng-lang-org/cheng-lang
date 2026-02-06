# The Future: Cheng 3 (the good)

*Cheng* is the name of the work-in-progress implementation for **Cheng 3**. The differences between Cheng 3 and Cheng 2 are listed here, briefly:

- Explicit `nil ref/ptr T` annotations, benefit: no more null pointer crashes at runtime.
- `fn mygeneric[T: <ConceptHere>](...) = ...`; benefit: increased compile-time checking.
- `case` inside `object` gets a new syntax. Benefit: Enables builtin pattern matching.
- Reworked `async` / `await` that is based on continuations. It mitigates the "what color is your function" problem:
  - Instead of `async` write `passive`.
  - Instead of `await` write nothing at all.
  - Async event loop unified with multi-threading: `spawn` will be part of the event loop.
- Reworked exception handling annotations. Benefit: Faster code.
- The effect system will be opt-in. Benefit: No more "GC safety" related compiler errors.
- `ref` will default to use atomic operations so the concept of "isolation" will not exist or not nearly be as important.
- `string` has a different implementation and it lost its terminating zero. So it does not convert to `cstring` without copies anymore. For the following reasons:
  1. Even in Cheng 2 this was not completely bulletproof: People cast between `seq[char]` and `string` which can lose the termination zero.
  2. It allows us to provide faster string slice operations.
  3. It saves memory.
- Macros are replaced by compiler plugins which will offer a different API. (We have no design for an API yet though.)
- Multi-methods are gone for good, use single dispatch methods.
- Cyclic module dependencies will be allowed if an explicit `cyclic` import statement is used: `import (path / module) {.cyclic.}`.
- Overloading based on `var T` is gone as accessors become polymorphic instead: It is tracked if a write to a location like `a[i].x.z` is allowed. It is allowed if `a` is mutable. The builtin array indexing has always worked this way so this is a most natural extension. This means:

```cheng
# Cheng 2 code
fn `[]`(x: var Container): var Element
fn `[]`(x: Container): lent Element
```

is simplified to:

```cheng
# Cheng
fn `[]`(x: Container): var Element # polymorphic accessor
```

It also means we know in the type system if a container access can resize the container or not as the resize would still require the var-ness for the parameter!

Overall, Cheng 3 will be more picky than Cheng 2 in some ways, more lenient in others.


# The present: Cheng (the bad)

The first pre-release of Cheng misses lots of things but might be useful for your needs if you are the kind of person that can write his own standard library.

Cheng currently lacks these features:

- Builtin pattern matching.
- `.passive` fns that are based on continuations.
- Cyclic module dependencies have not been implemented.
- An exception handling system that is compatible with Cheng's. Instead Cheng uses its own based on an `ErrorCode` enum.
- Cheng does not check `range` subtypes and treats a `range` type like its underlying base type. For example `range[0..10]` is treated as `int`.
- Cheng's effect system:
  - `tags` annotations are ignored. Tag mismatches should produce warnings in Cheng 3 with the option to turn these warnings into errors.
  - `raises: []` is the new default and ignored. `raises: <list here>` is mapped to `.raises` (but this needs to be changed to an exception system compatible with Cheng's).
  - `noSideEffect` and `func` is ignored. In the future we will probably add `.noSideEffect` inference and `func` strictness.
  - `gcsafe` is ignored.


# Open questions (the ugly)

Cheng is case sensitive. It is unclear at this point if Cheng 3 should keep Cheng 2's partial case sensitivity as in my experience this feature didn't pull its weight: Libraries can still use wildly different styles like routines using UpperCase and partial case sensitivity doesn't help. Enforcing the style via the compiler seems to work better.

Like Cheng 2, Cheng compiles the code via a C compiler. An LLVM based backend is in development but it's unclear which path to embrace officially.

Implicit generics where routines can leave out the `[T]` section but instead are generic as they use a type class such as `SomeInt` are not supported in Cheng and it is unclear if they should be supported by Cheng 3.
