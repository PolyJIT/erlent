#define FUSE_USE_VERSION 30
extern "C" {
#include <fuse.h>
#include <sys/types.h>
#include <sys/wait.h>
}

#include "erlent/erlent.hh"
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

class RemoteRequestProcessor : public RequestProcessor
{
public:
    RemoteRequestProcessor() {
        cout << unitbuf;
    }

    int process(Request &req) override {
        ostream &os = cout;
        istream &is = cin;
        Reply &repl = req.getReply();

        if (doLocally(req)) {
            req.performLocally();
        } else {
            req.serialize(os);
            os.flush();
            repl.receive(is);
        }

        return repl.getResult();
    }
};

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
    RemoteRequestProcessor reqproc;
    int opt, usercmd;

    dbg() << unitbuf;

    char cwd[PATH_MAX];
    char *newwd = getcwd(cwd, sizeof(cwd));

    bool chrootDefaults = false;
    while ((opt = getopt(argc, argv, "+l:L:Cw:dh")) != -1) {
        switch(opt) {
        case 'l': reqproc.appendLocalPath(optarg);  break;
        case 'L': reqproc.appendRemotePath(optarg); break;
        case 'C': chrootDefaults = true; break;
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
