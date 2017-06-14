#include <algorithm>
#include <mutex>
#include <string>
#include "erlent/local.hh"

extern "C" {
#include <dirent.h>
}

using namespace std;

const string erlent::LocalRequestProcessor::emuPrefix = ".erlent";

// remove all but one (if any) trailing slashes
static string removeTrailingSlashes(string path) {
    bool endsWithSlash = *path.rbegin() == '/';
    while (*path.rbegin() == '/')
        path = path.substr(0, path.length()-1);
    return endsWithSlash ? path+"/" : path;
}

void erlent::LocalRequestProcessor::addPathMapping(AttrType attrType, const string &inside, const string &outside)
{
    auto less = [](const PathProp &left, const PathProp &right) {
        return left.insidePath.length() > right.insidePath.length();
    };
    PathProp pp(attrType, removeTrailingSlashes(inside), removeTrailingSlashes(outside));
    paths.insert(paths.begin(), pp);
    std::sort(paths.begin(), paths.end(), less);
}

erlent::LocalRequestProcessor::AttrType erlent::LocalRequestProcessor::getAttrType(const erlent::Request &req) const
{
    const RequestWithPathname *rwp = dynamic_cast<const RequestWithPathname *>(&req);
    return rwp == nullptr ? AttrType::Untranslated : getAttrType(rwp->getPathname());
}

erlent::LocalRequestProcessor::AttrType erlent::LocalRequestProcessor::getAttrType(const string &pathname) const
{
    const PathProp *pp = findPathProp(pathname);
    return pp == nullptr ? AttrType::Untranslated : pp->attrType;
}

static bool isPathPrefixOf(const string &prefix, const string &path) {
    if (*prefix.rbegin() == '/')
        return path.find(prefix) == 0;
    return path == prefix || path.find(prefix + "/") == 0;
}

const erlent::LocalRequestProcessor::PathProp *erlent::LocalRequestProcessor::findPathProp(const string &pathname) const
{
    // only translate absolute paths, i.e., path beginning with '/'.
    if (pathname.find('/') != 0)
        return nullptr;
    std::vector<PathProp>::const_iterator it, end = paths.end();
    for (it=paths.begin(); it!=end; ++it) {
        const PathProp &pp = *it;
        if (isPathPrefixOf(pp.insidePath, pathname)) {
            return &pp;
        }
    }
    return nullptr;
}

static string pathConcat(const string &p1, const string &p2) {
    if (p1[p1.length()-1] == '/') {
        if (p2.length() > 0 && p2[0] == '/')
            return p1 + p2.substr(1);
        else
            return p1 + p2;
    } else if (p2.length() > 0 && p2[0] == '/')
        return p1 + p2;
    return p1 + "/" + p2;
}

void erlent::LocalRequestProcessor::translatePath(Request &req) const {
    RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
    if (rwp == nullptr)
        return;
    const string &pathname = rwp->getPathname();
    rwp->setPathname(translatePath(pathname));
    RequestWithTwoPathnames *rw2p = dynamic_cast<RequestWithTwoPathnames *>(&req);
    if (rw2p != nullptr) {
        rw2p->setPathname2(translatePath(rw2p->getPathname2()));
    }
}

string erlent::LocalRequestProcessor::translatePath(const string &pathname) const
{
    const PathProp *pp = findPathProp(pathname);
    if (pp == nullptr)
        return pathname;
    return pathConcat(pp->outsidePath, pathname.substr(pp->insidePath.length()));
}

static bool needsLock(const erlent::Request &req) {
    using namespace erlent;
    switch(req.getMessageType()) {
    case Message::OPEN:
    case Message::READ:
    case Message::READDIR:
    case Message::READLINK:
    case Message::STATFS:
    case Message::TRUNCATE:
    case Message::WRITE: return false;
    default: return true;
    }
}

int erlent::LocalRequestProcessor::process(Request &req) {
    static std::mutex m;

    bool nL = needsLock(req);
    if (nL)
        m.lock();
    int res = do_process(req);
    if (nL)
        m.unlock();
    return res;
}

int erlent::LocalRequestProcessor::do_process(Request &req) {
    AttrType attrType = getAttrType(req);

    translatePath(req);
    Reply &repl = req.getReply();
    RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
    const string *pathname = rwp != nullptr ? &rwp->getPathname() : nullptr;
    RequestWithTwoPathnames *rw2p = dynamic_cast<RequestWithTwoPathnames *>(&req);
    const string *pathname2 = rw2p != nullptr ? &rw2p->getPathname2() : nullptr;

    switch(attrType) {
    case AttrType::Emulated: {
        if (pathname != nullptr) {
            if (isEmuFile(*pathname))
                return -EPERM;
            if (pathname2 != nullptr && isEmuFile(*pathname2))
                return -EPERM;
            dbg() << "performing request on '" << *pathname << "'" << endl;
        }

        OpenRequest *openreq = dynamic_cast<OpenRequest *>(&req);
        if (openreq != nullptr) {
            openreq->setFlags(openreq->getFlags() & ALLOWED_OPEN_FLAGS_MASK);
        }

        UidGid *ug = dynamic_cast<UidGid *>(&req);
        if (ug != nullptr) {
            ug->setUid(uid2inner(ug->getUid()));
            ug->setGid(gid2inner(ug->getGid()));
        }
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
                emu_creat_mkdir(repl, *pathname, DIRECTORY, origMode, mkdirreq->getUid(), mkdirreq->getGid());
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
                emu_creat_mkdir(repl, symlinkreq->getPathname(), FILE,
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
                DIRFILE dt = S_ISDIR(buf->st_mode) ? DIRECTORY : FILE;
                if (readAttrs(*pathname, dt, &a) == 0) {
                    buf->st_uid = uid2outer(a.uid);
                    buf->st_gid = gid2outer(a.gid);
                    buf->st_mode = (buf->st_mode & ~ATTR_MASK) | (a.mode & ATTR_MASK);
                } else {
//                    cerr << "ERROR: readAttrs failed for " << *pathname << " (assuming" << (dt == DIR ? "DIR" : "FILE")
//                         << " from mode 0" << oct << buf->st_mode << endl;
                    // something went really wrong in readAttrs (because readAttrs returns default
                    // values on ENOENT), set uid/gid to (outer) root (which will be mapped to
                    // nobody/nogroup in the container)
                    buf->st_uid = 0;
                    buf->st_gid = 0;
                    buf->st_mode = buf->st_mode & ~(S_IRWXG | S_IRWXO);
                }
                if (dt == DIRECTORY) {
                    // Correct the st_size field for directories.
                    // (st_size is the number of files/dirs in
                    // the directory; we must not count the
                    // emulation files)
                    struct dirent *ent;
                    DIR *dir = opendir(pathname->c_str());
                    if (dir != NULL) {
                        buf->st_size = 0;
                        while ((ent = readdir(dir)) != NULL) {
                            if (!isEmuFile(ent->d_name))
                                ++buf->st_size;
                        }
                        closedir(dir);
                    }
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
            // the attributes file first but save its contents in case
            // the rmdir fails.
            Attrs a;
            readAttrs(rmdirreq->getPathname(), DIRECTORY, &a);
            unlink(attrsFileName(rmdirreq->getPathname(), DIRECTORY).c_str());
            rmdirreq->performLocally();
            if (repl.getResult() != 0)
                writeAttrs(rmdirreq->getPathname(), DIRECTORY, &a);
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
        break;
    }
    case AttrType::Mapped: {
        GetattrRequest *getattrreq = dynamic_cast<GetattrRequest *>(&req);
        if (getattrreq != nullptr) {
            GetattrReply &garepl = getattrreq->getReply();
            getattrreq->performLocally();
            if (garepl.getResult() == 0) {
                struct stat *buf = garepl.getStbuf();
                if (buf->st_uid == getuid() || buf->st_uid == geteuid())
                    buf->st_uid = uid2outer(params->initialUID);
                int n = getgroups(0, NULL);
                gid_t *groups = new gid_t[n+2];
                groups[0] = getgid();
                groups[1] = getegid();
                int nn = getgroups(n, &groups[2]);
                if (nn >= 0) {
                    if (nn > n)
                        nn = n;
                    nn += 2;
                    for (int i=0; i<nn; ++i) {
                        if (groups[i] == buf->st_gid) {
                            buf->st_gid = gid2outer(params->initialGID);
                            break;
                        }
                    }
                }
                delete[] groups;
            }
        } else
            req.performLocally();
        break;
    }
    case AttrType::Untranslated:
        req.performLocally();
        break;
    }
    dbg() << "(local) result is " << repl.getResultMessage() << endl;
    return repl.getResult();
}
