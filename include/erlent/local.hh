#ifndef _ERLENT_LOCAL_HH
#define _ERLENT_LOCAL_HH

extern "C" {
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
}
#include <string>

#include "erlent/child.hh"
#include "erlent/erlent.hh"

namespace erlent {

using namespace std;

class LocalRequestProcessor : public RequestProcessor
{
private:
    struct PathProp {
        bool doLocally;
        std::string insidePath;
        std::string outsidePath;

        PathProp(bool doLocally, const std::string &inside, const std::string &outside)
            : doLocally(doLocally), insidePath(inside), outsidePath(outside) { }
    };

    std::vector<PathProp> paths;

public:
    void addPathMapping(bool doLocally, const std::string &inside, const std::string &outside);

private:
    bool doLocally(const Request &req) const;
    bool doLocally(const std::string &pathname) const;
    void makePathLocal(Request &req) const;
    std::string makePathLocal(const std::string &pathname) const;

private:
    static const string emuPrefix;
    static const mode_t ATTR_MASK = S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO;

    enum DIRFILE {
        DIR = 1, FILE = 2
    };

    const erlent::ChildParams *params;

    static int write32(int fd, uint32_t val) {
        val = htonl(val);
        return write(fd, &val, sizeof(val)) == sizeof(val) ? 0 : -1;
    }

    static int read32(int fd, uint32_t &val) {
        int res = read(fd, &val, sizeof(val));
        val = ntohl(val);
        return res == sizeof(val) ? 0 : -1;
    }

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
        DIRFILE dt = S_ISDIR(buf.st_mode) ? DIR : FILE;
        return dt;
    }

    int openAttrsFile(const string &pathname, DIRFILE dt, int flags) {
        return open(attrsFileName(pathname, dt).c_str(), flags, filemode);
    }

    struct Attrs {
        uid_t uid;
        gid_t gid;
        mode_t mode;
        int write(int fd) const {
            bool ok;
            ok = write32(fd, uid) != -1;
            ok = ok && (write32(fd, gid) != -1);
            ok = ok && (write32(fd, mode) != -1);
            return ok ? 0 : -1;
        }
        int read(int fd) {
            bool ok;
            ok = read32(fd, uid) != -1;
            ok = ok && read32(fd, gid) != -1;
            ok = ok && read32(fd, mode) != -1;
            return ok ? 0 : -1;
        }
    };

    int readAttrs(const string &pathname, DIRFILE dt, Attrs *a) {
        // cerr << "readAttrs " << pathname << " " << (dt == DIR ? "DIR" : "FILE") << endl;
        int fd = openAttrsFile(pathname, dt, O_RDONLY);
        if (fd == -1) {
            if (errno == ENOENT) {
                struct stat buf;
                if (lstat(pathname.c_str(), &buf) == -1)
                    return -1;
                a->uid  = 0;
                a->gid  = 0;
                a->mode = buf.st_mode & ATTR_MASK;
                return 0;
            } else
                return -1;
        }
        int res = a->read(fd);
        close(fd);
        return res;
    }

    int writeAttrs(const string &pathname, DIRFILE dt, const Attrs *a) {
        // cerr << "writeAttrs " << pathname << " " << (dt == DIR ? "DIR" : "FILE") << endl;
        int fd = openAttrsFile(pathname, dt, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd == -1)
            return -1;
        int res = a->write(fd);
        close(fd);
        return res;
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
        size_t lastslash = name.rfind('/');
        string basename = lastslash == string::npos ? name : name.substr(lastslash+1);
        return basename.find(emuPrefix) == 0;
    }

    bool attr_emulation = true;
    mode_t filemode = S_IRUSR | S_IWUSR;
    mode_t dirmode  = S_IRWXU;

    // uid/gid -1 is used with chown(2) to mean "no change"
    uid_t uid2outer(uid_t uid) const { return uid == (uid_t)-1 ? uid : params->lookupUID(uid); }
    gid_t gid2outer(gid_t gid) const { return gid == (gid_t)-1 ? gid : params->lookupGID(gid); }
    uid_t uid2inner(uid_t uid) const { return uid == (uid_t)-1 ? uid : params->inverseLookupUID(uid); }
    gid_t gid2inner(gid_t gid) const { return gid == (gid_t)-1 ? gid : params->inverseLookupGID(gid); }

public:
    void setParams(const ChildParams &params) {
        this->params = &params;
    }

    int process(Request &req) override;
};

}
#endif // _ERLENT_LOCAL_HH
