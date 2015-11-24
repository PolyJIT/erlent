#ifndef _ERLENT_FUSE_HH
#define _ERLENT_FUSE_HH

#include "erlent/erlent.hh"

#include <sys/types.h>

int erlent_fuse(erlent::RequestProcessor &rp, bool devprocsys, const char *newWorkDir,
                char *const *args, uid_t new_uid, gid_t new_gid);

#endif // _ERLENT_FUSE_HH
