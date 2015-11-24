#ifndef _ERLENT_FUSE_HH
#define _ERLENT_FUSE_HH

#include "erlent/child.hh"
#include "erlent/erlent.hh"

#include <sys/types.h>

int erlent_fuse(erlent::RequestProcessor &rp,
                char *const *args, const ChildParams &params);

#endif // _ERLENT_FUSE_HH
