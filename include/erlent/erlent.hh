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
    public:
        virtual Type getMessageType() const = 0;
        virtual std::ostream &serialize(std::ostream &os) const = 0;
        virtual std::istream &deserialize(std::istream &is) = 0;

        static std::string typeName(Type ty);
    };

    class Reply;

    class Request : public Message {
    public:
        static Request *receive(std::istream &is);
        int process(Reply &repl) const;
        virtual void perform(std::ostream &os) = 0;

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);
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

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);
    };

    class GetattrReply : public Reply {
        struct stat *stbuf;
    public:
        GetattrReply(struct stat *stbuf) : stbuf(stbuf) { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::GETATTR; }
    };

    class GetattrRequest : public Request {
        std::string pathname;
    public:
        explicit GetattrRequest() { }
        GetattrRequest(const char *pathname) : pathname(pathname) { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::GETATTR; }
        void perform(std::ostream &os);
    };

    class ReaddirReply : public Reply {
        std::vector<std::string> names;
    public:
        typedef std::vector<std::string>::const_iterator name_iterator;

        ReaddirReply() : Reply() { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::READDIR; }

        void addName(const std::string &name) { names.push_back(name); }

        name_iterator names_begin() const { return names.begin(); }
        name_iterator names_end()   const { return names.end();   }
    };

    class ReaddirRequest : public Request {
        std::string pathname;
    public:
        explicit ReaddirRequest() { }
        ReaddirRequest(const char *pathname)
            : pathname(pathname) { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::READDIR; }
        void perform(std::ostream &os);
    };

    class ReadReply : public Reply {
        char *data;
        size_t len;  // len of 'data' array
    public:
        ReadReply(char *data, ssize_t len)
            : Reply(), data(data), len(len) { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::READ; }
    };

    class ReadRequest : public Request {
        std::string filename;
        size_t size;
        off_t offset;

    public:
        explicit ReadRequest() { }

        ReadRequest(const char *filename, size_t size, off_t offset)
            : Request(), filename(filename), size(size), offset(offset) { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Request::READ; }

        void perform(std::ostream &os);
    };

    class WriteReply : public Reply {
    public:
        WriteReply() { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::WRITE; }
    };

    class WriteRequest : public Request {
        std::string filename;
        const char *data;
        size_t size;
        off_t offset;
        bool del_data;
    public:
        explicit WriteRequest() { }

        WriteRequest(const char *filename, const char *data, size_t size, off_t offset)
            : Request(), filename(filename), data(data), size(size), offset(offset), del_data(false) { }

        ~WriteRequest() {
            if (del_data)
                delete data;
        }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Request::WRITE; }

        void perform(std::ostream &os);
    };

    class OpenReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::OPEN; }
    };

    class OpenRequest : public Request {
        std::string filename;
        int flags;
        mode_t mode;
    public:
        OpenRequest() { }
        OpenRequest(const char *filename, int flags, mode_t mode)
            : filename(filename), flags(flags), mode(mode) { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Request::OPEN; }

        void perform(std::ostream &os);
    };

    template<Message::Type MSGTYPE, typename VALTY>
    class PathValRequestTempl : public Request {
    protected:
        std::string filename;
        VALTY val;
    public:
        PathValRequestTempl() { }
        PathValRequestTempl(const char *filename, VALTY val)
            : filename(filename), val(val) { }

        std::ostream &serialize(std::ostream &os) const {
            this->Request::serialize(os);
            writestr(os, filename);
            writenum(os, val);
            return os;
        }

        std::istream &deserialize(std::istream &is) {
            this->Request::deserialize(is);
            readstr(is, filename);
            readnum(is, val);
            return is;
        }

        Request::Type getMessageType() const { return MSGTYPE; }

        virtual void perform(std::ostream &os) = 0;
    };

    template<Message::Type MSGTYPE>
    class PathRequestTempl : public Request {
    protected:
        std::string filename;
    public:
        PathRequestTempl() { }
        PathRequestTempl(const char *filename)
            : filename(filename) { }

        std::ostream &serialize(std::ostream &os) const {
            this->Request::serialize(os);
            writestr(os, filename);
            return os;
        }

        std::istream &deserialize(std::istream &is) {
            this->Request::deserialize(is);
            readstr(is, filename);
            return is;
        }

        Request::Type getMessageType() const { return MSGTYPE; }

        virtual void perform(std::ostream &os) = 0;
    };


    class TruncateReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::TRUNCATE; }
    };

    class TruncateRequest : public PathValRequestTempl<Message::TRUNCATE, off_t> {
    public:
        using PathValRequestTempl::PathValRequestTempl;
        void perform(std::ostream &os);
    };

    class ChmodReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::CHMOD; }
    };

    class ChmodRequest : public PathValRequestTempl<Message::CHMOD, mode_t> {
    public:
        using PathValRequestTempl::PathValRequestTempl;
        void perform(std::ostream &os);
    };

    class MkdirReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::MKDIR; }
    };

    class MkdirRequest : public PathValRequestTempl<Message::MKDIR, mode_t> {
    public:
        using PathValRequestTempl::PathValRequestTempl;
        void perform(std::ostream &os);
    };

    class UnlinkReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::UNLINK; }
    };

    class UnlinkRequest : public PathRequestTempl<Message::UNLINK> {
    public:
        using PathRequestTempl::PathRequestTempl;
        void perform(std::ostream &os);
    };

    class RmdirReply : public Reply {
    public:
        Request::Type getMessageType() const { return Message::RMDIR; }
    };

    class RmdirRequest : public PathRequestTempl<Message::RMDIR> {
    public:
        using PathRequestTempl::PathRequestTempl;
        void perform(std::ostream &os);
    };
}

#endif // ifndef _ERLENT_ERLENT_HH
