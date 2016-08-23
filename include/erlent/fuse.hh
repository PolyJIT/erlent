#ifndef _ERLENT_FUSE_HH
#define _ERLENT_FUSE_HH

#include "erlent/child.hh"
#include "erlent/erlent.hh"

#include <string>

#include <sys/types.h>

pid_t erlent_fuse(pid_t child_pid, erlent::RequestProcessor &rp);

#endif // _ERLENT_FUSE_HH
