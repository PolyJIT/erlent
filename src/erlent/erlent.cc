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

template<typename T>
static ostream &writenum(ostream &os, T value) {
    os << value << '\0';
    return os;
}

template<typename T>
static istream &readnum(istream &is, T &var) {
    string str;
    is >> str;
    istringstream(str) >> var;
    return is;
}

static ostream &writestr(ostream &os, const string &str) {
    writenum(os, str.length());
    os << str;
    return os;
}

static istream &readstr(istream &is, string &str) {
    int len;
    readnum(is, len);
    char cstr[len+1];
    is.read(cstr, len);
    cstr[len] = '\0';
    str = cstr;
    return is;
}

ostream &Message::serialize(ostream &os) const
{
    writenum(os, getMessageType());
    return os;
}

Request *Request::receive(istream &is)
{
    int msgtype;
    Request *req;

    readnum(is, msgtype);
    switch(msgtype) {
    case READ:
        req = new ReadRequest();
        req->deserialize(is);
        break;
    default:
        cerr << "Received unknown message type " << msgtype
             << ", cannot continue." << endl;
        exit(1);
    }
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

ostream &Reply::serialize(ostream &os) const
{
    writenum(os, getResult());
    return os;
}

istream &Reply::deserialize(istream &is)
{
    readnum(is, result);
    return is;
}

int Request::process(Reply &repl) const
{
    ostream &os = cout;
    istream &is = cin;

    serialize(os);
    repl.receive(is);

    return repl.getResult();
}

ostream &ReadRequest::serialize(ostream &os) const
{
    Request::serialize(os);
    writestr(os, filename);
    writenum(os, size);
    writenum(os, offset);
    return os;
}

istream &ReadRequest::deserialize(istream &is) {
    readstr(is, filename);
    readnum(is, size);
    readnum(is, offset);
    return is;
}

void ReadRequest::perform(ostream &os)
{
    int res = 0;
    char *data = new char[size];
    if (data) {
        int fd = open(filename.c_str(), O_RDONLY);
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



ostream &ReadReply::serialize(ostream &os) const
{
    this->Reply::serialize(os);
    if (getResult() > 0)
        os.write(data, getResult());
    return os;
}

istream &ReadReply::deserialize(istream &is)
{
    this->Reply::deserialize(is);
    if (getResult() > 0)
        is.read(data, getResult());
    return is;
}


ostream &ReaddirRequest::serialize(ostream &os) const
{
    this->Request::serialize(os);
    writestr(os, pathname);
    return os;
}

istream &ReaddirRequest::deserialize(istream &is)
{
    this->Request::deserialize(is);
    readstr(is, pathname);
    return is;
}

void ReaddirRequest::perform(ostream &os)
{
    int res;
    ReaddirReply rr;

    DIR *dir;
    dir = opendir(pathname.c_str());
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


ostream &ReaddirReply::serialize(ostream &os) const
{
    writenum(os, names.size());

    vector<string>::const_iterator it, end = names.end();
    for (it=names.begin(); it!=end; ++it) {
        writestr(os, *it);
    }
    return os;
}

istream &ReaddirReply::deserialize(istream &is)
{
    int n;
    readnum(is, n);
    while (n-- > 0) {
        string str;
        readstr(is, str);
        names.push_back(str);
    }
    return is;
}
