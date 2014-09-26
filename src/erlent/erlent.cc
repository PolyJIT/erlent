#include "erlent/erlent.hh"

#include <cstring>

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

ostream &erlent::dbg() { return dbgnull /*std::cerr*/; }

char erlent::getnextchar(istream &is) {
    int c = is.get();
    if (c == EOF) {
        // std::cerr << "End of input." << endl;
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
    case GETATTR: return "Getattr";
    case READDIR: return "Readdir";
    case READ:    return "Read";
    case WRITE:   return "Write";
    case OPEN:    return "Open";
    case TRUNCATE: return "Truncate";
    case CHMOD:    return "Chmod";
    case MKDIR:    return "Mkdir";
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
    case READDIR:  req = new ReaddirRequest();  break;
    case READ:     req = new ReadRequest();     break;
    case WRITE:    req = new WriteRequest();    break;
    case OPEN:     req = new OpenRequest();     break;
    case TRUNCATE: req = new TruncateRequest(); break;
    case CHMOD:    req = new ChmodRequest();    break;
    case MKDIR:    req = new MkdirRequest();    break;
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

int Request::process(Reply &repl) const
{
    ostream &os = cout;
    istream &is = cin;

    serialize(os);
    os.flush();
    repl.receive(is);

    return repl.getResult();
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

void ReaddirRequest::perform(ostream &os)
{
    int res;
    ReaddirReply rr;

    DIR *dir;
    dir = opendir(getPathname().c_str());
    if (dir != NULL) {
        errno = 0;
        struct dirent *de = readdir(dir);
        while (de) {
            rr.addName(de->d_name);
            errno = 0;
            de = readdir(dir);
        }
        res = -errno;
        closedir(dir);
    } else
        res = -errno;

    rr.setResult(res);
    rr.serialize(os);
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


void GetattrRequest::perform(ostream &os)
{
    int res;
    struct stat stbuf;
    GetattrReply repl(&stbuf);
    const string &pathname = getPathname();
    dbg() << "stating '" << pathname << "'." << endl;
    res = stat(pathname.c_str(), &stbuf);
    if (res == -1)
        res = -errno;
    dbg() << "result is " << strerror(-res) << endl;
    repl.setResult(res);
    repl.serialize(os);
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
}

void ReadRequest::perform(ostream &os)
{
    int res = 0;
    char *data = new char[size];
    if (data) {
        int fd = open(getPathname().c_str(), O_RDONLY);
        if (fd != -1) {
            if (lseek(fd, offset, SEEK_SET) != -1) {
                res = read(fd, data, size);
                if (res == -1)
                    res = -errno;
            } else
                res = -errno;

            close(fd);
        }
    } else
        res = -ENOMEM;

    ReadReply rr(data, size);
    rr.setResult(res);
    rr.serialize(os);

    delete[] data;
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
    if (getResult() > 0)
        is.read(data, getResult());
}


void WriteRequest::serialize(ostream &os) const
{
    this->RequestWithPathname<Message::WRITE>::serialize(os);
    writenum(os, size);
    writenum(os, offset);
    os.write(data, size);
}

void WriteRequest::deserialize(istream &is)
{
    this->RequestWithPathname<Message::WRITE>::deserialize(is);
    readnum(is, size);
    readnum(is, offset);
    char *datap = new char[size];
    is.read(datap, size);
    data = datap;
    del_data = true;
}

void WriteRequest::perform(ostream &os)
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
    }

    WriteReply repl;
    repl.setResult(res);
    repl.serialize(os);
}

void WriteReply::serialize(ostream &os) const
{
    this->Reply::serialize(os);
}

void WriteReply::deserialize(istream &is)
{
    this->Reply::deserialize(is);
}

void OpenRequest::serialize(ostream &os) const
{
    this->RequestWithPathname<Message::OPEN>::serialize(os);
    writenum(os, flags);
    writenum(os, mode);
}

void OpenRequest::deserialize(istream &is)
{
    this->RequestWithPathname<Message::OPEN>::deserialize(is);
    readnum(is, flags);
    readnum(is, mode);
}

void OpenRequest::perform(ostream &os)
{
    int res = 0;
    int fd = open(getPathname().c_str(), flags, mode);
    if (fd < 0)
        res = -errno;
    else
        close(fd);
    OpenReply repl;
    repl.setResult(res);
    repl.serialize(os);
}

void TruncateRequest::perform(ostream &os)
{
    int res = truncate(getPathname().c_str(), val);
    if (res < 0)
        res = -errno;
    TruncateReply repl;
    repl.setResult(res);
    repl.serialize(os);
}

void ChmodRequest::perform(ostream &os)
{
    int res = chmod(getPathname().c_str(), val);
    if (res < 0)
        res = -errno;
    ChmodReply repl;
    repl.setResult(res);
    repl.serialize(os);
}

void MkdirRequest::perform(ostream &os)
{
    int res = mkdir(getPathname().c_str(), val);
    if (res < 0)
        res = -errno;
    MkdirReply repl;
    repl.setResult(res);
    repl.serialize(os);
}

void UnlinkRequest::perform(ostream &os)
{
    int res = unlink(getPathname().c_str());
    if (res < 0)
        res = -errno;
    UnlinkReply repl;
    repl.setResult(res);
    repl.serialize(os);
}


void RmdirRequest::perform(ostream &os)
{
    int res = rmdir(getPathname().c_str());
    if (res < 0)
        res = -errno;
    RmdirReply repl;
    repl.setResult(res);
    repl.serialize(os);
}
