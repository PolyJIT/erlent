#ifndef _ERLENT_ERLENT_HH
#define _ERLENT_ERLENT_HH

#include <cstdint>
#include <ostream>
#include <vector>

namespace erlent {
    class Message {
    public:
        enum Type { READDIR, READ };
    protected:
        Message() { }
    public:
        virtual Type getMessageType() const = 0;
        virtual std::ostream &serialize(std::ostream &os) const = 0;
        virtual std::istream &deserialize(std::istream &is) = 0;
    };

    class Reply;

    class Request : public Message {
    public:
        static Request *receive(std::istream &is);
        int process(Reply &repl) const;
        virtual void perform(std::ostream &os) = 0;

        virtual std::ostream &serialize(std::ostream &os) const { return os; }
        virtual std::istream &deserialize(std::istream &is)     { return is; }
    };

    class Reply : public Message {
        int result;
    public:
        void receive(std::istream &is);
        int  getResult() const  { return result; }
        void setResult(int res) { result = res; }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);
    };

    class ReaddirReply : public Reply {
        std::vector<std::string> names;
    public:
        ReaddirReply() : Reply() { }

        std::ostream &serialize(std::ostream &os) const;
        std::istream &deserialize(std::istream &is);

        Request::Type getMessageType() const { return Message::READDIR; }

        void addName(const std::string &name) { names.push_back(name); }
    };

    class ReaddirRequest : public Request {
        std::string pathname;
    public:
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


    class ReqReadDir {
    };
    class ReplyReadDir {
    };

    class ReqGetAttr {
    };
    class ReplyGetAttr {
    };
}

#endif // ifndef _ERLENT_ERLENT_HH
