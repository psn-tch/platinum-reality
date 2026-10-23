// Test-only, opt-in (FRANKENSTEIN_FIXTURE_PTRACER): preloaded into a disposable fixture browser so
// gdb can attach when it hangs under Yama ptrace_scope=1. Never part of a release.
#include <sys/prctl.h>

__attribute__((constructor)) static void allowTracing(void)
{
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
}
