# R (`cpp11`) Backend Guidelines — `include/pyrabind/r/AGENTS.md`

Rules for files in `include/pyrabind/r/` and `src/r/`:

1. **Header order**: Include `<cpp11/R.hpp>` (or another `cpp11` header) before any `<Rinternals.h>` or `<R_ext/*>` header so `R_NO_REMAP` and `STRICT_R_HEADERS` stay active.
2. **Exception & GC safety**:
   - Catch every C++ exception inside an R boundary and translate typed exceptions (`value_error`, `type_error`, `index_error`, `key_error`) into R conditions / `Rf_errorcall`.
   - Execute fallible allocating R C-API calls through `cpp11::unwind_protect` so R `longjmp` unwinds C++ destructors safely.
   - Return scalar primitives (`Rf_ScalarReal`, `Rf_ScalarInteger`, `Rf_ScalarLogical`) directly when no further R allocations follow, avoiding unnecessary preserve-list churn.
3. **Fast `.External` dispatch**:
   - Decode `.External` pairlist arguments onto fixed stack storage (`pos[16]`, `kw[16]`), allocating on the heap only when more than 16 arguments are passed.
   - Resolve methods and properties through hashed R environments initialized once per class in `r/pyrabind.R`.
4. **Object lifetimes & symbol registration**:
   - Preserve `rv_policy::reference_internal` and `keep_alive<Nurse, Patient>` chains via `R_ExternalPtrProtected`.
   - Register `.Call` and `.External` entry points with `R_registerRoutines` using `std::memcpy` for `DL_FUNC` conversion.
