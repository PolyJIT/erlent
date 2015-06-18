#ifndef _ERLENT_FUSE_HH
#define _ERLENT_FUSE_HH

#include "erlent/erlent.hh"

int erlent_fuse(erlent::RequestProcessor &rp, const char *newWorkDir,
                char *const *args);

#endif // _ERLENT_FUSE_HH
