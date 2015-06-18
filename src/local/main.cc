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

        return repl.getResult();
    }
};

static pid_t child_pid;

static void startchild(int argc, char *argv[])
{
    int tochild[2], toparent[2];
    if (pipe(tochild) == -1 || pipe(toparent)) {
        int err = errno;
        cerr << "Error creating pipes: " << strerror(err) << endl;
        exit(1);
    }
    child_pid = fork();
    if (child_pid == -1) {
        int err = errno;
        cerr << "Error in fork: " << strerror(err) << endl;
        exit(1);
    } else if (child_pid == 0) {

        close(tochild[1]);
        close(toparent[0]);
        dup2(tochild[0], 0);
        dup2(toparent[1], 1);
        close(tochild[0]);
        close(toparent[1]);

        char **args = new char* [argc+1];
        for (int i=0; i<argc; ++i)
            args[i] = argv[i];
        args[argc] = 0;

        execvp(args[0], args);
        int err = errno;
        cerr << "Could not execute '" << argv[0] << "': " << strerror(err) << endl;
        exit(127);
    }

    close(tochild[0]);
    close(toparent[1]);

    dup2(tochild[1], 1);
    dup2(toparent[0], 0);

    close(tochild[1]);
    close(toparent[0]);
}

static void usage(const char *progname)
{
    cerr << "USAGE: " << progname << " [-l PATH] [-L PATH] [-w DIR] [-d] [-h] [--] CMD ARGS..." << endl
         << "   -l PATH      perform operations on PATH locally"
         << "   -L PATH      perform operations on PATH remotely"
         << "   -C           use default -l/-L settings for chroots"
         << "   -w DIR       change working directory to DIR" << endl
         << "   -d           Turn debug messagen on" << endl
         << "   -h           print this help" << endl
         << "   CMD ARGS...  command to execute and its arguments" << endl;
}

int main(int argc, char *argv[])
{
    LocalRequestProcessor reqproc;
    int opt, usercmd;

    child_pid = 0;

    dbg() << unitbuf;

    char cwd[PATH_MAX];
    char *newwd = getcwd(cwd, sizeof(cwd));

    bool chrootDefaults = false;
    while ((opt = getopt(argc, argv, "+l:L:Cr:w:dh")) != -1) {
        switch(opt) {
        case 'l': reqproc.appendLocalPath(optarg);  break;
        case 'L': reqproc.appendRemotePath(optarg); break;
        case 'C': chrootDefaults = true; break;
        case 'r': reqproc.setChrootDir(optarg); break;
        case 'w': newwd = optarg; break;
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
        reqproc.prependRemotePath("/sys");
        reqproc.prependRemotePath("/proc");
        reqproc.prependRemotePath("/dev");
        reqproc.prependLocalPath("/");
    }

    return erlent_fuse(reqproc, newwd, args);
}
