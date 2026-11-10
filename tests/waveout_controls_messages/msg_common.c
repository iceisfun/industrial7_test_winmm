#include "msg_common.h"

int wcm_dirs(wcm_dir d[2])
{
    d[0] = (wcm_dir){ "out", (wcm_msg_fn)U(waveOutMessage), wcm_nout(), 1 };
    d[1] = (wcm_dir){ "in", (wcm_msg_fn)U(waveInMessage), wcm_nin(), 0 };
    return d[0].fn && d[1].fn;
}

/* WaveMapperInit loads the mapper when there is any wave device at all (in or out). */
int wcm_dir_has_mapper(const wcm_dir *d) { (void)d; return wcm_mapper_present(); }
