#ifndef _ERLENT_CHILD_HH
#define _ERLENT_CHILD_HH

extern "C" {
#include <sys/types.h>
}

pid_t setup_child(uid_t new_uid, gid_t new_gid, bool devprocsys, const char *newRootDir, const char *newWorkDir,
                  char *const *args);
void run_child();
int wait_for_pid(pid_t p);

#endif // _ERLENT_CHILD_HH
