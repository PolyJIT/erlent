#ifndef _ERLENT_ERLENT_HH
#define _ERLENT_ERLENT_HH

#include <cstring>
#include <cstdint>
#include <ostream>
#include <iostream>
#include <set>
#include <vector>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
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
    std::istream &readstr (std::istream &is, std::string &str);

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
        enum Type { GETATTR=42, READDIR, READ, WRITE, OPEN, TRUNCATE, CHMOD,
                    MKDIR, UNLINK, RMDIR };
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

//        typedef RequestWithPathnameTempl<ReplyTy,MsgType> Super;
    public:
        RequestWithPathname() { }
        RequestWithPathname(const char *pathname)
            : pathname(pathname) { }
        const std::string &getPathname() const { return pathname; }
        void setPathname(const char *pathname) {
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
        /*
        RequestWithPathnameTempl() : RequestWithPathname() { }
        RequestWithPathnameTempl(const char *pathname)
            : RequestWithPathname(pathname) { }
            */
    public:
        Message::Type getMessageType() const { return MsgType; }

        ReplyTy &getReply() { return reply; }
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

    class ReaddirReply : public ReplyTempl<Message::READDIR> {
        std::vector<std::string> names;
    public:
        typedef std::vector<std::string>::const_iterator name_iterator;

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        Message::Type getMessageType() const { return Message::READDIR; }

        void addName(const std::string &name) { names.push_back(name); }

        name_iterator names_begin() const { return names.begin(); }
        name_iterator names_end()   const { return names.end();   }
    };

    class ReaddirRequest : public RequestWithPathnameTempl<ReaddirReply, Message::READDIR> {
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

    class OpenReply : public ReplyTempl<Message::OPEN> {
    };

    class OpenRequest : public RequestWithPathnameTempl<OpenReply,Message::OPEN> {
        int flags;
        mode_t mode;
    public:
        OpenRequest() { }
        OpenRequest(const char *pathname, int flags, mode_t mode)
            : RequestWithPathnameTempl(pathname), flags(flags), mode(mode) { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void performLocally();
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
    };

    class MkdirReply : public ReplyTempl<Message::MKDIR> {
    };

    class MkdirRequest : public RequestWithPathVal<MkdirReply, Message::MKDIR, mode_t> {
    public:
        using RequestWithPathVal::RequestWithPathVal;
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

    class RequestProcessor {
    private:
        struct PathProp {
            bool performLocally;
            std::string path;

            PathProp(bool local, const std::string &p)
                : performLocally(local), path(p) { }
        };

        std::vector<PathProp> paths;

    public:
        virtual int process(Request &req) = 0;

        void appendLocalPath(const std::string &pathname);
        void appendRemotePath(const std::string &pathname);
        void prependLocalPath(const std::string &pathname);
        void prependRemotePath(const std::string &pathname);

        bool doLocally(const Request &req) const;
        bool doLocally(const std::string &pathname) const;
    };
}

#endif // ifndef _ERLENT_ERLENT_HH
