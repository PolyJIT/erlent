#include <string>
#include "erlent/local.hh"

using namespace std;

const string erlent::LocalRequestProcessor::emuPrefix = ".erlent";

int erlent::LocalRequestProcessor::process(Request &req) {
    makePathLocal(req);
    Reply &repl = req.getReply();
    RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
    const string *pathname = rwp != nullptr ? &rwp->getPathname() : nullptr;
    if (pathname != nullptr) {
        dbg() << "performing request on '" << pathname << "'" << endl;
    }
    if (attr_emulation) {
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
                    buf->st_uid = uid2outer(a.uid);
                    buf->st_gid = gid2outer(a.gid);
                    buf->st_mode = (buf->st_mode & ~ATTR_MASK) | (a.mode & ATTR_MASK);
                } else {
                    // something went really wrong in readAttrs (because readAttrs returns default
                    // values on ENOENT), set uid/gid to (outer) root (which will be mapped to
                    // nobody/nogroup in the container)
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
