// What the portable client code needs from the operating system (implemented in src/platform/<os>/).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// ---- implemented by the platform layer ----
void *slinky_gl_get_proc(const char *name);        // OpenGL entry point by name (needs a current context)
void slinky_log_write(const char *line);            // one line of diagnostics (stderr / debug output), no newline
// ---- implemented by the client (src/engine/protected/init.cpp), called by the platform layer ----
int slinky_setup(void *self);                       // start-up; runs on the engine thread and stays attached to the JVM
int slinky_disarm(void);                            // take the Java hooks out again; only valid on the start-up thread
int slinky_disarm_step(void);                       // one class of that pass, called once per frame (0 done, 1 more, else error)
int slinky_disarm_in_flight(void);                  // 1 while a stepped pass is half finished
void slinky_detach(void);                           // the start-up thread gives up its JVM attachment
void slinky_teardown(void);                         // frees everything slinky_setup built (engine thread, after slinky_disarm)
void slinky_logf(const char *fmt, ...);             // printf-style diagnostics via slinky_log_write
#ifdef __cplusplus
}
#endif
