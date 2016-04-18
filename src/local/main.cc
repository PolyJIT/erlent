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

static const string emuPrefix = ".erlent";
static const mode_t ATTR_MASK = S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO;

class LocalRequestProcessor : public RequestProcessor
{
    enum DIRFILE {
        DIR = 1, FILE = 2
    };
private:
    uid_t outerU0;
    gid_t outerG0;

    // We assume that 'pathname' does NOT end with '/'.
    // When "/" or a path without "/" is passed in, we return "/".
    string dirof(const string &pathname) {
        string::size_type pos = pathname.rfind('/');
        if (pos == 0 || pos == string::npos)
            return "/";
        const string &d = pathname.substr(0, pos);
        return d;
    }

    string fileof(const string &pathname) {
        string::size_type pos = pathname.rfind('/');
        if (pos == string::npos) {
            cerr << "fileof(\"" << pathname << "\") undefined, exiting." << endl;
            exit(1);
        }
        return pathname.substr(pos+1);
    }

    string attrsFileName(const string &pathname, DIRFILE dt) {
        string attrsFN = dt == DIR ? pathname + "/" + emuPrefix:
                                     dirof(pathname) + "/" + emuPrefix + "." + fileof(pathname);
        // cerr << "attrsFileName for " << pathname << " is " << attrsFN << endl;
        return attrsFN;
    }

    DIRFILE dirfile(const string &pathname) {
        struct stat buf;
        if (lstat(pathname.c_str(), &buf) == -1) {
            cerr << "Expected directory or file \"" << pathname << "\" does not exist." << endl;
        }
        DIRFILE dt = (buf.st_mode & S_IFDIR) ? DIR : FILE;
        return dt;
    }

    int openAttrsFile(const string &pathname, DIRFILE dt, int flags) {
        return open(attrsFileName(pathname, dt).c_str(), flags, filemode);
    }

    struct Attrs {
        uid_t uid;
        gid_t gid;
        mode_t mode;
    };

    int readAttrs(const string &pathname, DIRFILE dt, Attrs *a) {
        // cerr << "readAttrs " << pathname << " " << (dt == DIR ? "DIR" : "FILE") << endl;
        int fd = openAttrsFile(pathname, dt, O_RDONLY);
        if (fd == -1 && errno == ENOENT) {
            struct stat buf;
            if (lstat(pathname.c_str(), &buf) == -1)
                return -1;
            a->uid  = outerU0;
            a->gid  = outerG0;
            a->mode = buf.st_mode & ATTR_MASK;
            return 0;
        }
        int s = read(fd, a, sizeof(*a));
        close(fd);
        return s == sizeof(*a) ? 0 : -1;
    }

    int writeAttrs(const string &pathname, DIRFILE dt, const Attrs *a) {
        // cerr << "writeAttrs " << pathname << " " << (dt == DIR ? "DIR" : "FILE") << endl;
        int fd = openAttrsFile(pathname, dt, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd == -1)
            return -1;
        int s = write(fd, a, sizeof(*a));
        close(fd);
        return s == sizeof(*a) ? 0 : -1;
    }

    void emu_chown(Reply &repl, const string &pathname, uid_t uid, gid_t gid) {
        Attrs a;
        repl.setResult(-EIO);
        DIRFILE dt = dirfile(pathname);
        if (readAttrs(pathname, dt, &a) == -1)
            return;
        if (uid != (uid_t)-1)
            a.uid = uid;
        if (gid != (gid_t)-1)
            a.gid = gid;
        if (writeAttrs(pathname, dt, &a) == -1)
            return;
        repl.setResult(0);
    }

    void emu_chmod(Reply &repl, const string &pathname, mode_t mode) {
        Attrs a;
        repl.setResult(-EIO);
        DIRFILE dt = dirfile(pathname);
        if (readAttrs(pathname, dt, &a) == -1)
            return;
        a.mode = mode & ATTR_MASK;
        if (writeAttrs(pathname, dt, &a) == -1)
            return;
        repl.setResult(0);
    }

    void emu_creat_mkdir(Reply &repl, const string &pathname, DIRFILE dt, mode_t mode, uid_t uid, gid_t gid) {
        Attrs a, dirA;
        if (readAttrs(dirof(pathname), DIR, &dirA) == -1)
            return;
        a.mode = mode & ATTR_MASK;
        a.uid  = uid;
        a.gid  = (dirA.mode & S_ISGID) ? dirA.gid : gid;
        if (writeAttrs(pathname, dt, &a) == -1)
            repl.setResult(-EIO);
    }

    static bool isEmuFile(const string &name) {
        string::size_type l = name.length();
        return name.find(emuPrefix) == 0;
    }

    bool attr_emulation = true;
    mode_t filemode = S_IRUSR | S_IWUSR;
    mode_t dirmode  = S_IRWXU;
public:
    void setupRoot(uid_t uid, gid_t gid) {
        outerU0 = uid;
        outerG0 = gid;
    }

    int process(Request &req) override {
        makePathLocal(req);
        Reply &repl = req.getReply();
        RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
        const string *pathname = rwp != nullptr ? &rwp->getPathname() : nullptr;
        if (pathname != nullptr) {
            dbg() << "performing request on '" << pathname << "'" << endl;
        }
        if (attr_emulation) {
            ChownRequest *chownreq = dynamic_cast<ChownRequest *>(&req);
            ChmodRequest *chmodreq = dynamic_cast<ChmodRequest *>(&req);
            GetattrRequest *getattrreq = dynamic_cast<GetattrRequest *>(&req);
            CreatRequest *creatreq = dynamic_cast<CreatRequest *>(&req);
            MkdirRequest *mkdirreq = dynamic_cast<MkdirRequest *>(&req);
            ReaddirRequest *readdirreq = dynamic_cast<ReaddirRequest *>(&req);
            UnlinkRequest *unlinkreq = dynamic_cast<UnlinkRequest *>(&req);
            RmdirRequest *rmdirreq = dynamic_cast<RmdirRequest *>(&req);
            RenameRequest *renamereq = dynamic_cast<RenameRequest *>(&req);
            LinkRequest *linkreq = dynamic_cast<LinkRequest *>(&req);
            SymlinkRequest *symlinkreq = dynamic_cast<SymlinkRequest *>(&req);
            MknodRequest *mknodreq = dynamic_cast<MknodRequest *>(&req);
            if (chownreq != nullptr) {
                emu_chown(repl, *pathname, chownreq->getUid(), chownreq->getGid());
            } else if (chmodreq != nullptr) {
                emu_chmod(repl, *pathname, chmodreq->getMode());
            } else if (creatreq != nullptr) {
                mode_t origMode = creatreq->getMode();
                creatreq->setMode(filemode);
                creatreq->performLocally();
                if (repl.getResult() == 0)
                    emu_creat_mkdir(repl, *pathname, FILE, origMode, creatreq->getUid(), creatreq->getGid());
            } else if (mkdirreq != nullptr) {
                mode_t origMode = mkdirreq->getMode();
                mkdirreq->setMode(dirmode);
                mkdirreq->performLocally();
                if (repl.getResult() == 0)
                    emu_creat_mkdir(repl, *pathname, DIR, origMode, mkdirreq->getUid(), mkdirreq->getGid());
            } else if (linkreq != nullptr) {
                linkreq->performLocally();
                if (repl.getResult() == 0) {
                    int res = link(attrsFileName(linkreq->getPathname(), FILE).c_str(),
                                   attrsFileName(linkreq->getPathname2(), FILE).c_str());
                    if (res == -1)
                        repl.setResult(-EIO);
                }
            } else if (symlinkreq != nullptr) {
                symlinkreq->performLocally();
                if (repl.getResult() == 0) {
                    emu_creat_mkdir(repl, symlinkreq->getPathname2(), FILE,
                                    S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO, symlinkreq->getUid(), symlinkreq->getGid());
                }
            } else if (mknodreq != nullptr) {
                mode_t mode = mknodreq->getMode();
                mknodreq->setMode((mode & ~ATTR_MASK) | filemode);
                mknodreq->performLocally();
                if (repl.getResult() == 0)
                    emu_creat_mkdir(repl, mknodreq->getPathname(), FILE,
                                    mode, mknodreq->getUid(), mknodreq->getGid());
            } else if (getattrreq != nullptr) {
                GetattrReply &garepl = getattrreq->getReply();
                getattrreq->performLocally();
                if (garepl.getResult() == 0) {
                    Attrs a;
                    struct stat *buf = garepl.getStbuf();
                    DIRFILE dt = (buf->st_mode & S_IFDIR) ? DIR : FILE;
                    if (readAttrs(*pathname, dt, &a) == 0) {
                        buf->st_uid = a.uid;
                        buf->st_gid = a.gid;
                        buf->st_mode = (buf->st_mode & ~ATTR_MASK) | (a.mode & ATTR_MASK);
                    } else {
                        buf->st_uid = 0;
                        buf->st_gid = 0;
                        buf->st_mode = buf->st_mode & ~(S_IRWXG | S_IRWXO);
                    }
                }
            } else if (readdirreq != nullptr) {
                readdirreq->performLocally();
                ReaddirReply &rdr = readdirreq->getReply();
                rdr.filter([](const string &name) { return !isEmuFile(name); });
            } else if (unlinkreq != nullptr) {
                unlinkreq->performLocally();
                if (repl.getResult() == 0)
                    unlink(attrsFileName(unlinkreq->getPathname(), FILE).c_str());
            } else if (rmdirreq != nullptr) {
                // the directory can only be removed when it is empty, so delete
                // the attributes file first but same its contents in case
                // the rmdir fails.
                Attrs a;
                readAttrs(rmdirreq->getPathname(), DIR, &a);
                unlink(attrsFileName(rmdirreq->getPathname(), DIR).c_str());
                rmdirreq->performLocally();
                if (repl.getResult() != 0)
                    writeAttrs(rmdirreq->getPathname(), DIR, &a);
            } else if (renamereq != nullptr) {
                renamereq->performLocally();
                if (repl.getResult() == 0) {
                    if (dirfile(renamereq->getPathname2()) == FILE) {
                        rename(attrsFileName(renamereq->getPathname(), FILE).c_str(),
                               attrsFileName(renamereq->getPathname2(), FILE).c_str());
                    }
                }
            } else
                req.performLocally();
        } else
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
         << "   -r DIR        new root directory" << endl
         << "   -w DIR        change working directory to DIR after changing root" << endl
         << "   -C            set up /dev, /proc and /sys inside the new root" << endl
         << "   -m DIR:MNTPT  map DIR (from host) to MNTPT in new root" << endl
         << "   -u UID        run CMD with this real and effective user  id" << endl
         << "   -g GID        run CMD with this real and effective group id" << endl
         << "   -U I:O:C      map user  ids [I..I+C) to host users  [O..O+C)" << endl
         << "   -G I:O:C      map group ids [I..I+C) to host groups [O..O+C)" << endl
         << "   -d            print a few debug messages" << endl
         << "   -h            print this help" << endl
         << "   CMD ARGS...   command to execute and its arguments" << endl;
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

    while ((opt = getopt(argc, argv, "+r:w:Cm:u:g:U:G:dh")) != -1) {
        switch(opt) {
        case 'r': chrootDir = optarg; break;
        case 'w': params.newWorkDir = optarg; break;
        case 'C': params.devprocsys = true; break;
        case 'm':
            if (!ChildParams::addBind(optarg, params.bindMounts)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'u': params.initialUID = atol(optarg); break;
        case 'g': params.initialGID = atol(optarg); break;
        case 'U':
            if (!ChildParams::addMapping(optarg, params.uidMappings)) {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'G':
            if (!ChildParams::addMapping(optarg, params.gidMappings)) {
                usage(argv[0]);
                return 1;
            }
            break;
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
    reqproc.setupRoot(params.lookupUID(0), params.lookupGID(0));

    return erlent_fuse(reqproc, args, params);
}
