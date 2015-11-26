#include "erlent/erlent.hh"

#include <istream>
#include <ostream>
#include <iostream>

extern "C" {
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
}

using namespace std;
using namespace erlent;

#include "erlent/fuse.hh"

using namespace erlent;

#include <cstdio>
#include <cstring>
#include <climits>
#include <errno.h>
#include <fcntl.h>
#include <csignal>

#include <vector>

using namespace std;

class LocalRequestProcessor : public RequestProcessor
{
public:
    int process(Request &req) override {
        makePathLocal(req);
        Reply &repl = req.getReply();
        RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
        if (rwp != nullptr) {
            // follow symlinks (translate each target in the chain
            // to a local path)
            do {
                const char *path = rwp->getPathname().c_str();
                char target[PATH_MAX+1];
                dbg() << "performing request on '" << path << "'" << endl;
                ssize_t s = readlink(path, target, PATH_MAX);
                if (s == -1)
                    break;
                target[s] = '\0';
                if (target[0] == '/') {
                    rwp->setPathname(target);
                } else {
                    // relative path, does not filter "/../../../.." constructs!
                    string::size_type d = rwp->getPathname().find_last_of('/');
                    if (d != string::npos)
                        rwp->setPathname(rwp->getPathname().substr(0,d+1) + target);
                    else
                        rwp->setPathname(target);
                }
            } while(true);
        }
        req.performLocally();
        dbg() << "(local) result is " << repl.getResultMessage() << endl;
        return repl.getResult();
    }
};

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " <OPTIONS> [--] CMD ARGS..." << endl
         << endl
         << "Build time stamp: " << __DATE__ << " " << __TIME__ << endl
         << endl
         << "   -r DIR       new root directory" << endl
         << "   -w DIR       change working directory to DIR" << endl
         << "   -C           use default -l/-L settings for chroots:" << endl
         << "                   pass through /dev, /proc and /sys" << endl
         << "   -u UID       change user ID to UID" << endl
         << "   -g GID       change group ID to GID" << endl
         << "   -d           Turn debug messagen on" << endl
         << "   -h           print this help" << endl
         << "   CMD ARGS...  command to execute and its arguments" << endl;
}

int main(int argc, char *argv[])
{
    ChildParams params;
    string chrootDir("/");
    LocalRequestProcessor reqproc;
    int opt, usercmd;

    uid_t euid = geteuid();
    gid_t egid = getegid();

//    dbg() << unitbuf;

    char cwd[PATH_MAX];
    params.newWorkDir = getcwd(cwd, sizeof(cwd));

    while ((opt = getopt(argc, argv, "+Cr:w:u:g:dh")) != -1) {
        switch(opt) {
        case 'C': params.devprocsys = true; break;
        case 'r': chrootDir = optarg; break;
        case 'w': params.newWorkDir = optarg; break;
        case 'u': params.uidMappings.push_back(Mapping(atol(optarg), euid, 1)); break;
        case 'g': params.gidMappings.push_back(Mapping(atol(optarg), egid, 1)); break;
        case 'd': GlobalOptions::setDebug(true); break;
        case 'h': usage(argv[0]); return 0;
        default:
            usage(argv[0]); return 1;
        }
    }

    if (params.uidMappings.empty())
        params.uidMappings.push_back(Mapping(euid, euid, 1));
    if (params.gidMappings.empty())
        params.gidMappings.push_back(Mapping(egid, egid, 1));

    usercmd = optind;
    if (usercmd >= argc) {
        // no command is given
        usage(argv[0]);
        return 1;
    }

    int n_args = argc - usercmd;
    char **args = new char* [n_args+1];
    for (int i=0; i<n_args; ++i)
        args[i] = argv[i+usercmd];
    args[n_args] = 0;

    reqproc.addPathMapping(true, "", chrootDir);

    return erlent_fuse(reqproc, args, params);
}
