#ifndef MM64_TRACE_H
#define MM64_TRACE_H

/* N64Recomp emits TRACE_ENTRY() at the start of every function when
 * us.rev1.toml sets trace_mode = true. Release builds leave that off.
 * With -DMM64R_RESEARCH the macro records the function name. Without it
 * the macro is empty, so a research trace rebuild still links a normal binary
 * if the CMake option is left off. */

#ifdef MM64R_RESEARCH
#ifdef __cplusplus
extern "C" {
#endif
void mm64r_trace_entry(const char* name);
#ifdef __cplusplus
}
#endif
#define TRACE_ENTRY() mm64r_trace_entry(__func__)
#define TRACE_RETURN() ((void)0)
#else
#define TRACE_ENTRY() ((void)0)
#define TRACE_RETURN() ((void)0)
#endif

#endif
