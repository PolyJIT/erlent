#ifndef _ERLENT_CHILD_HH
#define _ERLENT_CHILD_HH

#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <sys/types.h>
}

class Mapping {
public:
    long innerID;
    long outerID;
    long count;

public:
    Mapping(long innerID, long outerID, long count)
        : innerID(innerID), outerID(outerID), count(count) { }
};

class ChildParams {
public:
    std::string newRoot = "/";
    std::string newWorkDir = "/";
    bool devprocsys = false;
    std::vector<std::pair<std::string,std::string>> bindMounts;

    std::vector<Mapping> uidMappings;
    std::vector<Mapping> gidMappings;

    uid_t initialUID;
    gid_t initialGID;
public:
    static bool addBind(const std::string &str, std::vector<std::pair<std::string,std::string>> &binds);
    static bool addMapping(const std::string &str, std::vector<Mapping> &mappings);

    static long lookupID(long inner, const std::vector<Mapping> &mapping);
    long lookupUID(long inner) { return lookupID(inner, uidMappings); }
    long lookupGID(long inner) { return lookupID(inner, gidMappings); }
};

pid_t setup_child(char *const *args, const ChildParams &params);
void run_child();
int wait_for_pid(pid_t p);

#endif // _ERLENT_CHILD_HH
