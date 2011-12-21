#ifndef __RPC_MAILBOX_MAILBOX_HPP__
#define __RPC_MAILBOX_MAILBOX_HPP__

#include "rpc/connectivity/cluster.hpp"

class mailbox_cluster_t;

/* `mailbox_t` is a receiver of messages. Construct it with a callback function
to handle messages it receives. To send messages to the mailbox, call the
`get_address()` method and then call `send()` on the address it returns. */

struct mailbox_t :
    public home_thread_mixin_t
{
private:
    friend class mailbox_cluster_t;

    mailbox_cluster_t *cluster;

    typedef int id_t;
    id_t mailbox_id;

    boost::function<void(std::istream &, const boost::function<void()> &)> callback;

    DISABLE_COPYING(mailbox_t);

public:
    struct address_t {

        /* Constructs a nil address */
        address_t();

        address_t(const address_t&);

        /* Tests if the address is nil */
        bool is_nil() const;

        /* Returns the peer on which the mailbox lives. If the address is nil,
        fails. */
        peer_id_t get_peer() const;

    private:
        friend std::ostream &operator<<(std::ostream &, mailbox_t::address_t);
        friend void send(mailbox_cluster_t *, mailbox_t::address_t, boost::function<void(std::ostream&)>);
        friend class mailbox_t;
        friend class mailbox_cluster_t;

        friend class ::boost::serialization::access;
        template<class Archive> void serialize(Archive & ar, UNUSED const unsigned int version) {
            ar & peer;
            ar & thread;
            ar & mailbox_id;
        }

        /* The peer on which the mailbox is located */
        peer_id_t peer;

        /* The thread on `peer` that the mailbox lives on */
        int thread;

        /* The ID of the mailbox */
        id_t mailbox_id;
    };

    mailbox_t(mailbox_cluster_t *, const boost::function<void(std::istream &, const boost::function<void()> &)> &);
    ~mailbox_t();

    address_t get_address();
};

inline std::ostream &operator<<(std::ostream &s, mailbox_t::address_t a) {
    return s << a.peer << ":" << a.thread << ":" << a.mailbox_id;
}

/* `send()` sends a message to a mailbox. It is safe to call `send()` outside of
a coroutine; it does not block. If the mailbox does not exist or the peer is
inaccessible, `send()` will silently fail. */

void send(
    mailbox_cluster_t *src,
    mailbox_t::address_t dest,
    boost::function<void(std::ostream&)> message
    );

/* `mailbox_cluster_t` is a subclass of `connectivity_cluster_t` that adds
message routing infrastructure. */

struct mailbox_cluster_t : public connectivity_cluster_t {

public:
    explicit mailbox_cluster_t(int port);

protected:
    /* It's impossible to send a message to a mailbox without having its
    address, and it's impossible to transfer its address from another machine
    without sending a message. `send_utility_message()` is a way of
    "bootstrapping" the system. It sends a message directly to another peer;
    the other peer's `on_utility_message()` method will be called when the
    message arrives. The semantics are the same as with
    `connectivity_cluster_t`'s `send_message()`/`on_message()`. */
    void send_utility_message(peer_id_t, boost::function<void(std::ostream&)>);
    virtual void on_utility_message(peer_id_t, std::istream&, const boost::function<void()> &) = 0;

private:
    friend class mailbox_t;
    friend void send(mailbox_cluster_t *, mailbox_t::address_t, boost::function<void(std::ostream&)>);

    struct mailbox_table_t {
        mailbox_table_t();
        ~mailbox_table_t();
        mailbox_t::id_t next_mailbox_id;
        std::map<mailbox_t::id_t, mailbox_t*> mailboxes;
        mailbox_t *find_mailbox(mailbox_t::id_t);
    };
    one_per_thread_t<mailbox_table_t> mailbox_tables;

    static void write_utility_message(std::ostream&, boost::function<void(std::ostream&)> writer);
    static void write_mailbox_message(std::ostream&, int dest_thread, mailbox_t::id_t dest_mailbox_id, boost::function<void(std::ostream&)> writer);
    void on_message(peer_id_t, std::istream&);

    message_service_t::handler_registration_t message_handler_registration;
};

#endif /* __RPC_MAILBOX_MAILBOX_HPP__ */