#ifndef _ERLENT_CHILD_HH
#define _ERLENT_CHILD_HH

#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <sys/types.h>
}

class ChildParams {
public:
    std::string newRoot = "/";
    std::string newWorkDir = "/";
    bool devprocsys = false;
    std::vector<std::pair<std::string,std::string> > bindMounts;
};

pid_t setup_child(uid_t new_uid, gid_t new_gid, char *const *args, const ChildParams &params);
void run_child();
int wait_for_pid(pid_t p);

#endif // _ERLENT_CHILD_HH
