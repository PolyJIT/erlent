#ifndef _ERLENT_ERLENT_HH
#define _ERLENT_ERLENT_HH

#include <cstring>
#include <cstdint>
#include <ostream>
#include <iostream>
#include <vector>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
}


namespace erlent {
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
        enum Type { GETATTR, READDIR, READ, WRITE, OPEN, TRUNCATE, CHMOD,
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
        int process(Reply &repl) const;
        virtual void perform(std::ostream &os) = 0;

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);
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

    template<enum Message::Type MsgType>
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

        void serialize(std::ostream &os) const {
            this->Request::serialize(os);
            writestr(os, pathname);
        }
        void deserialize(std::istream &is) {
            this->Request::deserialize(is);
            readstr(is, pathname);
        }

        virtual void perform(std::ostream &os) = 0;

        Message::Type getMessageType() const { return MsgType; }
    };


    class GetattrReply : public Reply {
        struct stat *stbuf;
    public:
        GetattrReply(struct stat *stbuf) : stbuf(stbuf) { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::GETATTR; }
    };

    class GetattrRequest : public RequestWithPathname<Message::GETATTR> {
    public:
        using RequestWithPathname::RequestWithPathname;

        void perform(std::ostream &os);
    };

    class ReaddirReply : public Reply {
        std::vector<std::string> names;
    public:
        typedef std::vector<std::string>::const_iterator name_iterator;

        ReaddirReply() : Reply() { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::READDIR; }

        void addName(const std::string &name) { names.push_back(name); }

        name_iterator names_begin() const { return names.begin(); }
        name_iterator names_end()   const { return names.end();   }
    };

    class ReaddirRequest : public RequestWithPathname<Message::READDIR> {
    public:
        using RequestWithPathname::RequestWithPathname;

        void perform(std::ostream &os);
    };

    class ReadReply : public Reply {
        char *data;
        size_t len;  // len of 'data' array
    public:
        ReadReply(char *data, ssize_t len)
            : Reply(), data(data), len(len) { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::READ; }
    };

    class ReadRequest : public RequestWithPathname<Message::READ> {
        size_t size;
        off_t offset;
    public:
        ReadRequest() { }

        ReadRequest(const char *pathname, size_t size, off_t offset)
            : RequestWithPathname(pathname), size(size), offset(offset) { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void perform(std::ostream &os);
    };

    class WriteReply : public Reply {
    public:
        WriteReply() { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::WRITE; }
    };

    class WriteRequest : public RequestWithPathname<Message::WRITE> {
        const char *data;
        size_t size;
        off_t offset;
        bool del_data;
    public:
        WriteRequest() { }
        WriteRequest(const char *pathname, const char *data, size_t size, off_t offset)
            : RequestWithPathname(pathname), data(data), size(size), offset(offset), del_data(false) { }

        ~WriteRequest() {
            if (del_data)
                delete data;
        }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void perform(std::ostream &os);
    };

    class OpenReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::OPEN; }
    };

    class OpenRequest : public RequestWithPathname<Message::OPEN> {
        int flags;
        mode_t mode;
    public:
        OpenRequest() { }
        OpenRequest(const char *pathname, int flags, mode_t mode)
            : RequestWithPathname(pathname), flags(flags), mode(mode) { }

        void serialize(std::ostream &os) const;
        void deserialize(std::istream &is);

        void perform(std::ostream &os);
    };

    template<Message::Type MsgType, typename VALTY>
    class RequestWithPathVal : public RequestWithPathname<MsgType> {
    protected:
        VALTY val;
    public:
        RequestWithPathVal() { }
        RequestWithPathVal(const char *pathname, VALTY val)
            : RequestWithPathname<MsgType>(pathname), val(val) { }

        void serialize(std::ostream &os) const {
            this->RequestWithPathname<MsgType>::serialize(os);
            writenum(os, val);
        }

        void deserialize(std::istream &is) {
            this->RequestWithPathname<MsgType>::deserialize(is);
            readnum(is, val);
        }

        virtual void perform(std::ostream &os) = 0;
    };

    class TruncateReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::TRUNCATE; }
    };

    class TruncateRequest : public RequestWithPathVal<Message::TRUNCATE, off_t> {
    public:
        using RequestWithPathVal::RequestWithPathVal;
        void perform(std::ostream &os);
    };

    class ChmodReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::CHMOD; }
    };

    class ChmodRequest : public RequestWithPathVal<Message::CHMOD, mode_t> {
    public:
        using RequestWithPathVal::RequestWithPathVal;
        void perform(std::ostream &os);
    };

    class MkdirReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::MKDIR; }
    };

    class MkdirRequest : public RequestWithPathVal<Message::MKDIR, mode_t> {
    public:
        using RequestWithPathVal::RequestWithPathVal;
        void perform(std::ostream &os);
    };

    class UnlinkReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::UNLINK; }
    };

    class UnlinkRequest : public RequestWithPathname<Message::UNLINK> {
    public:
        using RequestWithPathname::RequestWithPathname;
        void perform(std::ostream &os);
    };

    class RmdirReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::RMDIR; }
    };

    class RmdirRequest : public RequestWithPathname<Message::RMDIR> {
    public:
        using RequestWithPathname<Message::RMDIR>::RequestWithPathname;
        void perform(std::ostream &os);
    };
}

#endif // ifndef _ERLENT_ERLENT_HH
