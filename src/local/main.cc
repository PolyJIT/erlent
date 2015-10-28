#include "erlent/erlent.hh"

#include <istream>
#include <ostream>
#include <iostream>

extern "C" {
#include <unistd.h>
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

struct PathProp {
    bool performLocally;
    std::string path;

    PathProp(bool local, const string &p)
        : performLocally(local), path(p) { }
};

class LocalRequestProcessor : public RequestProcessor
{
private:
    std::string chrootDir;
public:
    LocalRequestProcessor() : chrootDir("/") {}

    void setChrootDir(const char *dir) {
        chrootDir = dir;
        if (*chrootDir.rend() != '/')
            chrootDir.push_back('/');
    }

    int process(Request &req) override {
        Reply &repl = req.getReply();

        if (!doLocally(req)) {
            RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
            if (rwp != nullptr) {
                std::string newPathname = chrootDir + rwp->getPathname();
                rwp->setPathname(newPathname.c_str());
            }
        }
        req.performLocally();
        dbg() << "(local) result is " << repl.getResultMessage() << endl;
        return repl.getResult();
    }
};

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " [-l PATH] [-L PATH] [-w DIR] [-d] [-h] [--] CMD ARGS..." << endl
         << "   -r DIR       new root directory" << endl
         << "   -w DIR       change working directory to DIR" << endl
         << "   -l PATH      map PATH relative to new root" << endl
         << "   -L PATH      map PATH to host directory" << endl
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
    LocalRequestProcessor reqproc;
    int opt, usercmd;

    uid_t euid = geteuid();
    gid_t egid = getegid();
    uid_t inner_uid = euid;
    gid_t inner_gid = egid;

    dbg() << unitbuf;

    char cwd[PATH_MAX];
    char *newwd = getcwd(cwd, sizeof(cwd));

    bool chrootDefaults = false;
    while ((opt = getopt(argc, argv, "+l:L:Cr:w:u:g:dh")) != -1) {
        switch(opt) {
        case 'l': reqproc.appendLocalPath(optarg);  break;
        case 'L': reqproc.appendRemotePath(optarg); break;
        case 'C': chrootDefaults = true; break;
        case 'r': reqproc.setChrootDir(optarg); break;
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

    if (chrootDefaults) {
        reqproc.prependLocalPath("/sys");
        reqproc.prependLocalPath("/proc");
        reqproc.prependLocalPath("/dev");
    }

    return erlent_fuse(reqproc, newwd, args, inner_uid, inner_gid);
}
