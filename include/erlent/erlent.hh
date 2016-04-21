#ifndef _ERLENT_ERLENT_HH
#define _ERLENT_ERLENT_HH

#include <cstring>
#include <cstdint>
#include <functional>
#include <ostream>
#include <iostream>
#include <set>
#include <vector>

extern "C" {
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
}


namespace erlent {
    class GlobalOptions {
    private:
            static bool debug;
    public:
            static bool isDebug();
            static void setDebug(bool dbg);
    };

    class EofException { };

    std::ostream &dbg();

    std::ostream &writestr(std::ostream &os, const std::string &str);
    std::istream &readstr (std::istream &is,       std::string &str);
    std::ostream &writetimespec(std::ostream &os, const struct timespec &ts);
    std::istream &readtimespec (std::istream &is,       struct timespec &ts);

    template<typename T>
    static inline std::ostream &writenum(std::ostream &os, T value) {
        os << std::dec << value << '\0';
        return os;
    }

    char getnextchar(std::istream &is);

    template<typename T>
    static std::istream &readnum(std::istream &is, T &var) {
        char c;
        bool neg = false;
        var = 0;
        do {
            c = getnextchar(is);
            if (c == '-')
                neg = true;
            else if (c != '\0')
                var = (var * 10) + (T)(c - '0');
        } while (c != '\0');
        if (neg)
            var = -var;
        return is;
    }


    class Message {
    public:
        enum Type { GETATTR=42, ACCESS, READDIR, READLINK, MKNOD,
                    READ, WRITE, OPEN, CREAT, TRUNCATE, CHMOD, CHOWN,
                    MKDIR, UNLINK, RMDIR, UTIMENS, SYMLINK, LINK, RENAME,
                    STATFS };
    protected:
        Message() { }
        virtual ~Message() { }
    public:
        virtual Type getMessageType() const = 0;
        virtual void serialize(std::ostream &os) const = 0;
        virtual void deserialize(std::istream &is) = 0;

        static std::string typeName(Type ty);
    };

    class Reply;

    class Request : public Message {
    public:
        static Request *receive(std::istream &is);

        virtual void perform(std::ostream &os);
        virtual void performLocally() = 0;

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        virtual Reply &getReply() = 0;
    };

    class Reply : public Message {
        int result;
    public:
        void receive(std::istream &is);
        int  getResult() const  { return result; }
        const char *getResultMessage() const {
            return result < 0 ? strerror(-result) : "Success";
        }
        void setResult(int res) { result = res; }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);
    };

    template<enum Message::Type MessageTy>
    class ReplyTempl : public Reply {
        Message::Type getMessageType() const { return MessageTy; }
    };

    class RequestWithPathname : public Request {
        std::string pathname;

    public:
        RequestWithPathname() { }
        RequestWithPathname(const char *pathname)
            : pathname(pathname) { }
        const std::string &getPathname() const { return pathname; }
        void setPathname(const char *pathname) {
            this->pathname = pathname;
        }
        void setPathname(const std::string &pathname) {
            this->pathname = pathname;
        }

        void serialize(std::ostream &os) const {
            this->Request::serialize(os);
            writestr(os, pathname);
        }

        void deserialize(std::istream &is) {
            this->Request::deserialize(is);
            readstr(is, pathname);
        }
    };

    template<typename ReplyTy, enum Message::Type MsgType>
    class RequestWithPathnameTempl : public RequestWithPathname {
    protected:
        typedef RequestWithPathnameTempl<ReplyTy,MsgType> Super;
        ReplyTy reply;
    public:
        using RequestWithPathname::RequestWithPathname;

    public:
        Message::Type getMessageType() const { return MsgType; }

        ReplyTy &getReply() { return reply; }
    };

    class RequestWithTwoPathnames : public RequestWithPathname {
        std::string pathname2;

    public:
        RequestWithTwoPathnames() { }
        RequestWithTwoPathnames(const char *pathname, const char *pathname2)
            : RequestWithPathname(pathname), pathname2(pathname2) { }
        const std::string &getPathname2() const { return pathname2; }
        void setPathname2(const char *pathname2) {
            this->pathname2 = pathname2;
        }
        void setPathname2(const std::string &pathname2) {
            this->pathname2 = pathname2;
        }

        void serialize(std::ostream &os) const {
            this->RequestWithPathname::serialize(os);
            writestr(os, pathname2);
        }

        void deserialize(std::istream &is) {
            this->RequestWithPathname::deserialize(is);
            readstr(is, pathname2);
        }
    };

    template<typename ReplyTy, enum Message::Type MsgType>
    class RequestWithTwoPathnamesTempl : public RequestWithTwoPathnames {
    protected:
        typedef RequestWithTwoPathnamesTempl<ReplyTy,MsgType> Super;
        ReplyTy reply;
    public:
        using RequestWithTwoPathnames::RequestWithTwoPathnames;

    public:
        Message::Type getMessageType() const { return MsgType; }

        ReplyTy &getReply() { return reply; }
    };

    template<typename ReplyTy, Message::Type MsgType, typename VALTY>
    class RequestWithPathVal : public RequestWithPathnameTempl<ReplyTy, MsgType> {
    protected:
        VALTY val;
    public:
        RequestWithPathVal() { }
        RequestWithPathVal(const char *pathname, VALTY val)
            : RequestWithPathnameTempl<ReplyTy, MsgType>(pathname), val(val) { }

        void serialize(std::ostream &os) const {
            this->RequestWithPathnameTempl<ReplyTy, MsgType>::serialize(os);
            writenum(os, val);
        }

        void deserialize(std::istream &is) {
            this->RequestWithPathnameTempl<ReplyTy, MsgType>::deserialize(is);
            readnum(is, val);
        }
    };

    class UidGid {
    private:
        uid_t uid;
        gid_t gid;
    public:
        void setUid(uid_t uid) { this->uid = uid; }
        void setGid(gid_t gid) { this->gid = gid; }
        uid_t getUid() const { return uid; }
        gid_t getGid() const { return gid; }
        void serialize(std::ostream &os) const {
            writenum(os, uid);
            writenum(os, gid);
        }
        void deserialize(std::istream &is) {
            readnum(is, uid);
            readnum(is, gid);
        }
    };

    class Mode {
    private:
        mode_t mode;
    public:
        void setMode(mode_t mode) { this->mode = mode; }
        mode_t getMode() const { return mode; }
        void serialize(std::ostream &os) const { writenum(os, mode); }
        void deserialize(std::istream &is) { readnum(is, mode); }
    };


    class GetattrReply : public ReplyTempl<Message::GETATTR> {
        struct stat *stbuf;
    public:
        void init(struct stat *stbuf) { this->stbuf = stbuf; }
        struct stat *getStbuf() { return stbuf; }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        Message::Type getMessageType() const { return Message::GETATTR; }
    };

    class GetattrRequest : public RequestWithPathnameTempl<GetattrReply, Message::GETATTR> {
    public:
        using Super::RequestWithPathnameTempl;

        void perform(std::ostream &os);
        void performLocally();
    };

    class AccessReply : public ReplyTempl<Message::ACCESS> {
    };

    class AccessRequest : public RequestWithPathnameTempl<AccessReply, Message::ACCESS> {
        int acc;
    public:
        AccessRequest() { }
        AccessRequest(const char *pathname, int acc)
            : RequestWithPathnameTempl(pathname), acc(acc) { }
        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);
        void performLocally();
    };

    class ReaddirReply : public ReplyTempl<Message::READDIR> {
        std::vector<std::string> names;
    public:
        typedef std::vector<std::string>::const_iterator name_iterator;

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void addName(const std::string &name) { names.push_back(name); }

        name_iterator names_begin() const { return names.begin(); }
        name_iterator names_end()   const { return names.end();   }

        void filter(std::function<bool (const std::string&)> pred);
    };

    class ReaddirRequest : public RequestWithPathnameTempl<ReaddirReply, Message::READDIR> {
    public:
        using Super::RequestWithPathnameTempl;

        void performLocally();
    };

    class ReadlinkReply : public ReplyTempl<Message::READLINK> {
        std::string target;
    public:
        void serialize(std::ostream &os) const {
            this->ReplyTempl<Message::READLINK>::serialize(os);
            writestr(os, target);
        }
        void deserialize(std::istream &is) {
            this->ReplyTempl<Message::READLINK>::deserialize(is);
            readstr(is, target);
        }

        void setTarget(const char *t) { target = t; }
        const std::string &getTarget() const { return target; }
    };

    class ReadlinkRequest : public RequestWithPathnameTempl<ReadlinkReply, Message::READLINK> {
    public:
        using Super::RequestWithPathnameTempl;

        void performLocally();
    };

    class ReadReply : public ReplyTempl<Message::READ> {
        char *data;
        size_t len;  // size of data array (to prevent buffer overruns)
    public:
        ReadReply() { }

        void init(char *data, size_t len) { this->data = data; this->len = len; }
        char *getData() { return data; }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);
    };

    class ReadRequest : public RequestWithPathnameTempl<ReadReply, Message::READ> {
        size_t size;
        off_t offset;
    public:
        ReadRequest() { }

        ReadRequest(const char *pathname, size_t size, off_t offset)
            : RequestWithPathnameTempl(pathname), size(size), offset(offset) { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void perform(std::ostream &os);
        void performLocally();
    };

    class WriteReply : public ReplyTempl<Message::WRITE> {
    };

    class WriteRequest : public RequestWithPathnameTempl<WriteReply, Message::WRITE> {
        const char *data;
        size_t size;
        off_t offset;
        bool del_data;
    public:
        WriteRequest() { }
        WriteRequest(const char *pathname, const char *data, size_t size, off_t offset)
            : RequestWithPathnameTempl(pathname), data(data), size(size), offset(offset), del_data(false) { }

        ~WriteRequest() {
            if (del_data)
                delete data;
        }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void performLocally();
    };

    class CreatReply : public ReplyTempl<Message::CREAT> {
    };

    class CreatRequest : public RequestWithPathnameTempl<CreatReply,Message::CREAT>, public UidGid, public Mode {
    public:
        using RequestWithPathnameTempl::RequestWithPathnameTempl;

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void performLocally();
    };

    class OpenReply : public ReplyTempl<Message::OPEN> {
    };

    class OpenRequest : public RequestWithPathnameTempl<OpenReply,Message::OPEN>, public Mode {
        int flags;
        mode_t mode;
    public:
        OpenRequest() { }
        OpenRequest(const char *pathname, int flags)
            : RequestWithPathnameTempl(pathname), flags(flags) { }

        int getFlags() const   { return flags; }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void performLocally();
    };

    class TruncateReply : public ReplyTempl<Message::TRUNCATE> {
    };

    class TruncateRequest : public RequestWithPathVal<TruncateReply, Message::TRUNCATE, off_t> {
    public:
        using RequestWithPathVal::RequestWithPathVal;
        void performLocally();
    };

    class ChmodReply : public ReplyTempl<Message::CHMOD> {
    };

    class ChmodRequest : public RequestWithPathVal<ChmodReply, Message::CHMOD, mode_t> {
    public:
        using RequestWithPathVal::RequestWithPathVal;
        void performLocally();

        mode_t getMode() const { return val; }
    };

    class MknodReply : public ReplyTempl<Message::MKNOD> {
    };

    class MknodRequest : public RequestWithPathVal<MknodReply, Message::MKNOD, dev_t>, public UidGid, public Mode {
    public:
        using RequestWithPathVal::RequestWithPathVal;
        void serialize(std::ostream &os) const {
            this->RequestWithPathVal::serialize(os);
            this->UidGid::serialize(os);
            this->Mode::serialize(os);
        }
        void deserialize(std::istream &is) {
            this->RequestWithPathVal::deserialize(is);
            this->UidGid::deserialize(is);
            this->Mode::deserialize(is);
        }

        void performLocally();
    };

    class ChownReply : public ReplyTempl<Message::CHOWN> {
    };

    class ChownRequest : public RequestWithPathnameTempl<ChownReply, Message::CHOWN>, public UidGid {
    public:
        ChownRequest() { }
        ChownRequest(const char *pathname)
            : RequestWithPathnameTempl<ChownReply, Message::CHOWN>(pathname) { }
        void serialize(std::ostream &os) const {
            this->RequestWithPathnameTempl<ChownReply, Message::CHOWN>::serialize(os);
            this->UidGid::serialize(os);
        }
        void deserialize(std::istream &is) {
            this->RequestWithPathnameTempl<ChownReply, Message::CHOWN>::deserialize(is);
            this->UidGid::deserialize(is);
        }

        void performLocally();
    };

    class SymlinkReply : public ReplyTempl<Message::SYMLINK> {
    };

    class SymlinkRequest : public RequestWithTwoPathnamesTempl<SymlinkReply,Message::SYMLINK>, public UidGid {
    public:
        using RequestWithTwoPathnamesTempl::RequestWithTwoPathnamesTempl;
        void serialize(std::ostream &os) const {
            this->RequestWithTwoPathnamesTempl::serialize(os);
            this->UidGid::serialize(os);
        }
        void deserialize(std::istream &is) {
            this->RequestWithTwoPathnamesTempl::deserialize(is);
            this->UidGid::deserialize(is);
        }

        void performLocally();
    };

    class LinkReply : public ReplyTempl<Message::LINK> {
    };

    class LinkRequest : public RequestWithTwoPathnamesTempl<LinkReply,Message::LINK> {
    public:
        using RequestWithTwoPathnamesTempl::RequestWithTwoPathnamesTempl;
        void performLocally();
    };


    class MkdirReply : public ReplyTempl<Message::MKDIR> {
    };

    class MkdirRequest : public RequestWithPathnameTempl<MkdirReply, Message::MKDIR>, public UidGid {
        mode_t mode;
    public:
        MkdirRequest() { }
        MkdirRequest(const char *pathname, mode_t mode)
            : RequestWithPathnameTempl(pathname), mode(mode) { }

        void setMode(mode_t mode) { this->mode = mode; }
        mode_t getMode() const { return mode; }

        void serialize(std::ostream &os) const {
            this->RequestWithPathnameTempl::serialize(os);
            this->UidGid::serialize(os);
            writenum(os, mode);

        }
        void deserialize(std::istream &is) {
            this->RequestWithPathnameTempl::deserialize(is);
            this->UidGid::deserialize(is);
            readnum(is, mode);
        }

        void performLocally();
    };

    class UnlinkReply : public ReplyTempl<Message::UNLINK> {
    };

    class UnlinkRequest : public RequestWithPathnameTempl<UnlinkReply, Message::UNLINK> {
    public:
        using Super::RequestWithPathnameTempl;
        void performLocally();
    };

    class RmdirReply : public ReplyTempl<Message::RMDIR> {
    };

    class RmdirRequest : public RequestWithPathnameTempl<RmdirReply, Message::RMDIR> {
    public:
        using Super::RequestWithPathnameTempl;
        void performLocally();
    };

    class UtimensReply : public ReplyTempl<Message::UTIMENS> {
    };

    class UtimensRequest : public RequestWithPathnameTempl<UtimensReply, Message::UTIMENS> {
        struct timespec times[2];
    public:
        UtimensRequest() { }
        UtimensRequest(const char *pathname, const struct timespec times[2])
            : RequestWithPathnameTempl(pathname) {
            this->times[0] = times[0];
            this->times[1] = times[1];
        }

        void serialize(std::ostream &os) const override {
            this->RequestWithPathname::serialize(os);
            writetimespec(os, times[0]);
            writetimespec(os, times[1]);
        }
        void deserialize(std::istream &is) override {
            this->RequestWithPathname::deserialize(is);
            readtimespec(is, times[0]);
            readtimespec(is, times[1]);
        }

        void performLocally();
    };

    class RenameReply : public ReplyTempl<Message::RENAME> {
    };

    class RenameRequest : public RequestWithTwoPathnamesTempl<RmdirReply, Message::RENAME> {
        std::string to;
    public:
        using RequestWithTwoPathnamesTempl::RequestWithTwoPathnamesTempl;

        void performLocally();
    };


    class StatfsReply : public ReplyTempl<Message::STATFS> {
        struct statvfs *buf;
    public:
        void init(struct statvfs *buf) { this->buf = buf; }
        struct statvfs *getStatvfsBuf() { return buf; }
        void serialize(std::ostream &os) const override;
        void deserialize(std::istream &is) override;
    };

    class StatfsRequest : public RequestWithPathnameTempl<StatfsReply, Message::STATFS> {
    public:
        using RequestWithPathnameTempl::RequestWithPathnameTempl;
        void perform(std::ostream &os);
        void performLocally();
    };


    class RequestProcessor {
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
        virtual int process(Request &req) = 0;

        void addPathMapping(bool doLocally, const std::string &inside, const std::string &outside);

        bool doLocally(const Request &req) const;
        bool doLocally(const std::string &pathname) const;
        void makePathLocal(Request &req) const;
        std::string makePathLocal(const std::string &pathname) const;
    };
}

#endif // ifndef _ERLENT_ERLENT_HH
