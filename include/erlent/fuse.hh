#ifndef _ERLENT_FUSE_HH
#define _ERLENT_FUSE_HH

#include "erlent/child.hh"
#include "erlent/erlent.hh"

#include <sys/types.h>

int erlent_fuse(erlent::RequestProcessor &rp,
                char *const *cmdArgs_, const erlent::ChildParams &params_);

#endif // _ERLENT_FUSE_HH
