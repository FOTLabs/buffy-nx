/* The game's sampler and frame consumer share counts and sample buffers.
 * Kernel calls may release the guest lock midway through either operation.
 * Keep their transactions separate, as on the original single-CPU runtime. */
#include "recomp_types.h"
#include <pthread.h>

extern void sub_00024B50(void);
extern void sub_00024E90(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);

static pthread_mutex_t s_input_lock = PTHREAD_MUTEX_INITIALIZER;
static RECOMP_TLS unsigned s_input_depth;

static void input_leave(void)
{
    if (!--s_input_depth)
        pthread_mutex_unlock(&s_input_lock);
}

void buffy_input_update(void)
{
    if (!s_input_depth) {
        if (pthread_mutex_trylock(&s_input_lock)) {
            int depth = xbox_gil_suspend();
            pthread_mutex_lock(&s_input_lock);
            xbox_gil_resume(depth);
        }
    }
    ++s_input_depth;
    sub_00024B50();
    input_leave();
}

void buffy_input_poll(void)
{
    if (!s_input_depth && pthread_mutex_trylock(&s_input_lock)) {
        /* A background sampler must not wait with a guest dispatch lock
         * held. Update polls synchronously for any frames it still needs. */
        g_esp += 4; /* original Poll is void(void), guest ret 0 */
        return;
    }
    ++s_input_depth;
    sub_00024E90();
    input_leave();
}