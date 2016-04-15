#include "erlent/erlent.hh"

#include <cstring>

#include <algorithm>
#include <istream>
#include <ostream>
#include <sstream>
#include <iostream>

extern "C" {
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

using namespace std;
using namespace erlent;

class nullostream : public ostream {
};

static nullostream dbgnull;

ostream &erlent::dbg() {
    return GlobalOptions::isDebug() ? std::cerr : dbgnull;
}

char erlent::getnextchar(istream &is) {
    int c = is.get();
    if (c == EOF) {
        dbg() << "End of input." << endl;
        throw EofException();
    }
    return (char)c;
}

ostream &erlent::writestr(ostream &os, const string &str) {
//    dbg() << "Writing '" << str << "'." << endl;
    writenum(os, str.length());
    os << str;
    return os;
}

istream &erlent::readstr(istream &is, string &str) {
    int len;
    readnum(is, len);
//    dbg() << "len = " << len << endl;
    char *cstr = (char *)alloca((len+1) * sizeof(char));
    is.read(cstr, len);
    if (is.gcount() != len) {
        fprintf(stderr, "Could not read %d bytes\n", len);
        throw EofException();
    }
    cstr[len] = '\0';
    str = cstr;
    return is;
}

string Message::typeName(Message::Type ty)
{
    switch(ty) {
    case GETATTR:  return "Getattr";
    case ACCESS:   return "Access";
    case READDIR:  return "Readdir";
    case READLINK: return "Readlink";
    case READ:     return "Read";
    case WRITE:    return "Write";
    case OPEN:     return "Open";
    case CREAT:    return "Creat";
    case TRUNCATE: return "Truncate";
    case CHMOD:    return "Chmod";
    case CHOWN:    return "Chown";
    case MKDIR:    return "Mkdir";
    case SYMLINK:  return "Symlink";
    case UNLINK:   return "Unlink";
    case RMDIR:    return "Rmdir";
    }
    return "(unknown, missing in Message::typeName)";
}

Request *Request::receive(istream &is)
{
    Message::Type msgtype;
    Request *req = 0;

    int imsgtype;
    readnum(is, imsgtype);
    msgtype = Message::Type(imsgtype);

    dbg() << "receive: msgtype=" << msgtype << endl;
    switch(msgtype) {
    case GETATTR:  req = new GetattrRequest();  break;
    case ACCESS:   req = new AccessRequest();   break;
    case READDIR:  req = new ReaddirRequest();  break;
    case READLINK: req = new ReadlinkRequest(); break;
    case READ:     req = new ReadRequest();     break;
    case WRITE:    req = new WriteRequest();    break;
    case OPEN:     req = new OpenRequest();     break;
    case CREAT:    req = new CreatRequest();    break;
    case TRUNCATE: req = new TruncateRequest(); break;
    case CHMOD:    req = new ChmodRequest();    break;
    case CHOWN:    req = new ChownRequest();    break;
    case MKDIR:    req = new MkdirRequest();    break;
    case SYMLINK:  req = new SymlinkRequest();  break;
    case UNLINK:   req = new UnlinkRequest();   break;
    case RMDIR:    req = new RmdirRequest();    break;
    }

    // We do not use a default: case since GCC generates
    // a warning for missing cases for an enum when no
    // default: case is present.
    if (!req) {
        cerr << "Received unknown message type " << msgtype
             << ", cannot continue." << endl;
        exit(1);
    }
    req->deserialize(is);
    return req;
}

void Reply::receive(istream &is)
{
    int msgtype;

    readnum(is, msgtype);
    if (msgtype != getMessageType()) {
        cerr << "Received wrong answer type (" << msgtype << ") instead\n"
             << "of expected type " << getMessageType() << ", cannot continue." << endl;
        exit(1);
    }

    deserialize(is);
}

void Reply::serialize(ostream &os) const
{
    dbg() << "Serializing " << typeName(getMessageType()) << " reply, result is "
          << getResult() << " (" << getResultMessage() << ")." << endl;
    writenum(os, getMessageType());
    writenum(os, getResult());
}

void Reply::deserialize(istream &is)
{
    dbg() << "Deserializing " << typeName(getMessageType()) << " reply." << endl;
    readnum(is, result);
}

void Request::perform(ostream &os)
{
    performLocally();
    getReply().serialize(os);
}

void Request::serialize(ostream &os) const
{
    dbg() << "Serializing " << typeName(getMessageType()) << " request." << endl;
    writenum(os, getMessageType());
}

void Request::deserialize(istream &is)
{
    dbg() << "Deserializing " << typeName(getMessageType()) << " request." << endl;
}

void ReaddirRequest::performLocally()
{
    ReaddirReply &rr = getReply();
    int res;
    DIR *dir;
    dir = opendir(getPathname().c_str());
    if (dir != NULL) {
        errno = 0;
        struct dirent *de = readdir(dir);
        while (de) {
            // cerr << " ### " << de->d_name << endl;
            rr.addName(de->d_name);
            errno = 0;
            de = readdir(dir);
        }
        res = -errno;
        closedir(dir);
    } else
        res = -errno;

    rr.setResult(res);
}


void ReaddirReply::serialize(ostream &os) const
{
    this->Reply::serialize(os);
    writenum(os, names.size());

    vector<string>::const_iterator it, end = names.end();
    for (it=names.begin(); it!=end; ++it) {
        writestr(os, *it);
    }
}

void ReaddirReply::deserialize(istream &is)
{
    this->Reply::deserialize(is);
    int n;
    readnum(is, n);
    while (n-- > 0) {
        string str;
        readstr(is, str);
        names.push_back(str);
    }
}

void ReaddirReply::filter(std::function<bool (const string &)> pred)
{
    auto it = names.begin();
    while (it != names.end()) {
        if (!pred(*it))
            it = names.erase(it);
        else
            ++it;
    }
}


void GetattrRequest::perform(ostream &os)
{
    struct stat stbuf;
    GetattrReply &repl = getReply();
    repl.init(&stbuf);
    performLocally();
    repl.serialize(os);
}

void GetattrRequest::performLocally()
{
    GetattrReply &repl = getReply();
    const string &pathname = getPathname();
    dbg() << "(l)stating '" << pathname << "'." << endl;
    int res = lstat(pathname.c_str(), repl.getStbuf());
    if (res == -1)
        res = -errno;
    repl.setResult(res);
}


void GetattrReply::serialize(ostream &os) const
{
    this->Reply::serialize(os);
    writenum(os, stbuf->st_mode);
    writenum(os, stbuf->st_nlink);
    writenum(os, stbuf->st_uid);
    writenum(os, stbuf->st_gid);
    writenum(os, stbuf->st_rdev);
    writenum(os, stbuf->st_size);
    writenum(os, stbuf->st_atime);
    writenum(os, stbuf->st_mtime);
    writenum(os, stbuf->st_ctime);
}

void GetattrReply::deserialize(istream &is)
{
    this->Reply::deserialize(is);
    readnum(is, stbuf->st_mode);
    readnum(is, stbuf->st_nlink);
    readnum(is, stbuf->st_uid);
    readnum(is, stbuf->st_gid);
    readnum(is, stbuf->st_rdev);
    readnum(is, stbuf->st_size);
    readnum(is, stbuf->st_atime);
    readnum(is, stbuf->st_mtime);
    readnum(is, stbuf->st_ctime);
}

void ReadRequest::serialize(ostream &os) const
{
    this->RequestWithPathname::serialize(os);
    writenum(os, size);
    writenum(os, offset);
}

void ReadRequest::deserialize(istream &is) {
    this->RequestWithPathname::deserialize(is);
    readnum(is, size);
    readnum(is, offset);
    dbg() << "ReadRequest for '" << getPathname() << "', " << size << ", " << offset << endl;
}

void ReadRequest::perform(ostream &os)
{
    ReadReply &rr = getReply();
    char *data = new char[size];
    if (data) {
        rr.init(data, size);
        performLocally();
    } else
        rr.setResult(-ENOMEM);
    rr.serialize(os);
    delete[] data;
}

void ReadRequest::performLocally()
{
    ReadReply &repl = getReply();
    int res;
    int fd = open(getPathname().c_str(), O_RDONLY);
    if (fd != -1) {
        if (lseek(fd, offset, SEEK_SET) != -1) {
            res = read(fd, repl.getData(), size);
            if (res == -1)
                res = -errno;
        } else
            res = -errno;

        close(fd);
    } else
        res = -errno;
    getReply().setResult(res);
}

void ReadReply::serialize(ostream &os) const
{
    this->Reply::serialize(os);
    if (getResult() > 0)
        os.write(data, getResult());
}

void ReadReply::deserialize(istream &is)
{
    dbg() << "deserializing ReadReply" << endl;
    this->Reply::deserialize(is);
    int res = getResult();
    if (res > 0 && (size_t)res <= len)
        is.read(data, res);
}


void WriteRequest::serialize(ostream &os) const
{
    this->Super::serialize(os);
    writenum(os, size);
    writenum(os, offset);
    os.write(data, size);
}

void WriteRequest::deserialize(istream &is)
{
    this->Super::deserialize(is);
    readnum(is, size);
    readnum(is, offset);
    char *datap = new char[size];
    is.read(datap, size);
    data = datap;
    del_data = true;
}

void WriteRequest::performLocally()
{
    int res = 0;
    int fd = open(getPathname().c_str(), O_WRONLY);
    if (fd != -1) {
        if (lseek(fd, offset, SEEK_SET) != -1) {
            res = write(fd, data, size);
            if (res == -1)
                res = -errno;
        } else
            res = -errno;

        close(fd);
    } else
        res = -errno;
    getReply().setResult(res);
}

void OpenRequest::serialize(ostream &os) const
{
    this->Super::serialize(os);
    writenum(os, flags);
    writenum(os, mode);
}

void OpenRequest::deserialize(istream &is)
{
    this->Super::deserialize(is);
    readnum(is, flags);
    readnum(is, mode);
}

void OpenRequest::performLocally()
{
    int res = 0;
    int fd = open(getPathname().c_str(), flags, mode);
    if (fd < 0)
        res = -errno;
    else
        close(fd);
    getReply().setResult(res);
}

void TruncateRequest::performLocally()
{
    int res = truncate(getPathname().c_str(), val);
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}

void ChmodRequest::performLocally()
{
    int res = chmod(getPathname().c_str(), val);
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}

void ChownRequest::performLocally()
{
    int res = chown(getPathname().c_str(), uid, gid);
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}

void MkdirRequest::performLocally()
{
    int res = mkdir(getPathname().c_str(), mode);
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}

void UnlinkRequest::performLocally()
{
    int res = unlink(getPathname().c_str());
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}

void RmdirRequest::performLocally()
{
    int res = rmdir(getPathname().c_str());
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}


bool GlobalOptions::debug = false;

bool GlobalOptions::isDebug()
{
    return debug;
}

void GlobalOptions::setDebug(bool dbg)
{
    debug = dbg;
}


void RequestProcessor::addPathMapping(bool doLocally, const string &inside, const string &outside)
{
    auto less = [](const PathProp &left, const PathProp &right) {
        return left.insidePath.length() > right.insidePath.length();
    };
    paths.insert(paths.begin(), PathProp(doLocally,inside,outside));
    sort(paths.begin(), paths.end(), less);
}

void RequestProcessor::makePathLocal(Request &req) const {
    RequestWithPathname *rwp = dynamic_cast<RequestWithPathname *>(&req);
    if (rwp == nullptr)
        return;
    const string &pathname = rwp->getPathname();
    std::vector<PathProp>::const_iterator it, end = paths.end();
    for (it=paths.begin(); it!=end; ++it) {
        const PathProp &pp = *it;
        if (pathname.find(pp.insidePath) == 0) {
            rwp->setPathname(pp.outsidePath + pathname.substr(pp.insidePath.length()));
            return;
        }
    }
}

bool RequestProcessor::doLocally(const Request &req) const {
    const RequestWithPathname *rwp = dynamic_cast<const RequestWithPathname *>(&req);
    return rwp != nullptr && doLocally(rwp->getPathname());
}

bool RequestProcessor::doLocally(const std::string &pathname) const {
    std::vector<PathProp>::const_iterator it, end = paths.end();
    for (it=paths.begin(); it!=end; ++it) {
        const PathProp &pp = *it;
        if (pathname.find(pp.insidePath) == 0)
            return pp.doLocally;
    }
    return false;
}


void AccessRequest::serialize(ostream &os) const
{
    this->RequestWithPathname::serialize(os);
    writenum(os, acc);
}

void AccessRequest::deserialize(istream &is) {
    this->RequestWithPathname::deserialize(is);
    readnum(is, acc);
}

void AccessRequest::performLocally()
{
    int res = access(getPathname().c_str(), acc);
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}


void SymlinkRequest::performLocally()
{
    int res = symlink(from.c_str(), getPathname().c_str());
    if (res < 0)
        res = -errno;
    getReply().setResult(res);
}


void ReadlinkRequest::performLocally()
{
    char target[PATH_MAX+1];
    int res = readlink(getPathname().c_str(), target, PATH_MAX);
    if (res == -1) {
        res = -errno;
    } else {
        // readlink does NOT 0-terminate the result string
        target[res] = '\0';
        getReply().setTarget(target);
        res = 0;
    }
    getReply().setResult(res);
}


void CreatRequest::serialize(ostream &os) const
{
    this->RequestWithPathname::serialize(os);
    writenum(os, mode);
    writenum(os, uid);
    writenum(os, gid);
}

void CreatRequest::deserialize(istream &is)
{
    this->RequestWithPathname::deserialize(is);
    readnum(is, mode);
    readnum(is, uid);
    readnum(is, gid);
}

void CreatRequest::performLocally()
{
    int res = 0;
    int fd = creat(getPathname().c_str(), mode);
    if (fd == -1)
        res = -errno;
    else
        close(fd);
    getReply().setResult(res);
}
