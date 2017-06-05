#ifndef _ERLENT_CHILD_HH
#define _ERLENT_CHILD_HH

#include <string>
#include <utility>
#include <vector>

#define FORK_DEBUG while(0)
// #define FORK_DEBUG if(0){}else

extern "C" {
#include <sys/types.h>
}

namespace erlent {

class Mapping {
public:
    unsigned long innerID;
    unsigned long outerID;
    unsigned long count;

public:
    Mapping(unsigned long innerID, unsigned long outerID, unsigned long count)
        : innerID(innerID), outerID(outerID), count(count) { }
};

class ChildParams {
public:
    std::string newWorkDir = "/";
    bool devprocsys = false;
    bool unshareNet = false;
    std::vector<std::pair<std::string,std::string>> bindMounts;

    std::vector<Mapping> uidMappings;
    std::vector<Mapping> gidMappings;

    uid_t initialUID;
    gid_t initialGID;
public:
    static bool addBind(const std::string &str, std::vector<std::pair<std::string,std::string>> &binds);
    static bool addMapping(const std::string &str, std::vector<Mapping> &mappings);

    static unsigned long lookupID(unsigned long inner, const std::vector<Mapping> &mapping);
    static unsigned long inverseLookupID(unsigned long outer, const std::vector<Mapping> &mapping);
    uid_t lookupUID(uid_t inner) const { return lookupID(inner, uidMappings); }
    gid_t lookupGID(gid_t inner) const { return lookupID(inner, gidMappings); }
    uid_t inverseLookupUID(uid_t outer) const { return inverseLookupID(outer, uidMappings); }
    gid_t inverseLookupGID(gid_t outer) const { return inverseLookupID(outer, gidMappings); }
    bool existsGidMapping(gid_t inner) const;

};

pid_t setup_child(char *const *args, ChildParams params);
void run_child(const std::string &newRoot);
void wait_child_chroot();
void parent_fuse_preclean();
int wait_for_pid(pid_t p, const std::initializer_list<pid_t> &forward_to);

}

#endif // _ERLENT_CHILD_HH
