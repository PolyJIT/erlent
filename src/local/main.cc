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
            dbg() << "performing request on '" << rwp->getPathname() << "'" << endl;
            struct stat st;
            if (stat(rwp->getPathname().c_str(), &st) == 0) {
                if (S_ISCHR(st.st_mode) && dynamic_cast<TruncateRequest *>(&req)) {
                    dbg() << "(local:) ignoring request to truncate character device" << endl;
                    return 0;
                }
            }
        }
        req.performLocally();
        dbg() << "(local) result is " << repl.getResultMessage() << endl;
        return repl.getResult();
    }
};

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " [-l PATH] [-L PATH] [-w DIR] [-r DIR] [-d] [-h] [--] CMD ARGS..." << endl
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
    string chrootDir("/");
    LocalRequestProcessor reqproc;
    int opt, usercmd;

    uid_t euid = geteuid();
    gid_t egid = getegid();
    uid_t inner_uid = euid;
    gid_t inner_gid = egid;

    dbg() << unitbuf;

    char cwd[PATH_MAX];
    char *newwd = getcwd(cwd, sizeof(cwd));

    bool devprocsys = false;
    while ((opt = getopt(argc, argv, "+Cr:w:u:g:dh")) != -1) {
        switch(opt) {
        case 'C': devprocsys = true; break;
        case 'r': chrootDir = optarg; break;
        case 'w': newwd = optarg; break;
        case 'u': inner_uid = atol(optarg); break;
        case 'g': inner_gid = atol(optarg); break;
        case 'd': GlobalOptions::setDebug(true); break;
        case 'h': usage(argv[0]); return 0;
        default:
            usage(argv[0]); return 1;
        }
    }
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

    return erlent_fuse(reqproc, devprocsys, newwd, args, inner_uid, inner_gid);
}
