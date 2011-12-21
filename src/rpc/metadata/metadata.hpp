#ifndef __RPC_METADATA_METADATA_HPP__
#define __RPC_METADATA_METADATA_HPP__

#include "rpc/mailbox/mailbox.hpp"
#include "rpc/metadata/view.hpp"

/* `metadata_cluster_t` is a `mailbox_cluster_t` that uses the utility message
system to synchronize a value, called the "cluster metadata", between all of the
nodes in the cluster. `metadata_cluster_t` is templatized on the type of the
cluster metadata. The type `metadata_t` must satisfy the following constraints:

1. It must have public and sane default constructor, copy constructor,
    copy assignment, and destructor.

2. It must be serializable using `boost::serialization`.

3. There must exist a function:

        void semilattice_join(metadata_t *a, const metadata_t &b);

    such that `metadata_t` is a semilattice and `semilattice_join(a, b)` sets
    `*a` to the semilattice-join of `*a` and `b`.

`metadata_cluster_t` is currently not thread-safe at all. */

template<class metadata_t>
class metadata_cluster_t :
    public mailbox_cluster_t
{
public:
    metadata_cluster_t(int port, const metadata_t &initial_metadata);
    ~metadata_cluster_t();

    boost::shared_ptr<metadata_readwrite_view_t<metadata_t> > get_root_view();

private:
    /* `get_root_view()` returns a pointer to this. It just exists to implement
    `metadata_readwrite_view_t` for us. */
    class root_view_t : public metadata_readwrite_view_t<metadata_t> {
    public:
        explicit root_view_t(metadata_cluster_t *);
        metadata_cluster_t *parent;
        metadata_t get();
        void join(const metadata_t &);
        void sync_from(peer_id_t, signal_t *) THROWS_ONLY(interrupted_exc_t, sync_failed_exc_t);
        void sync_to(peer_id_t, signal_t *) THROWS_ONLY(interrupted_exc_t, sync_failed_exc_t);
        publisher_t<boost::function<void()> > *get_publisher();
    };
    boost::shared_ptr<root_view_t> root_view;

    metadata_t metadata;

    void join_metadata_locally(metadata_t);

    /* Infrastructure for notifying things when metadata changes */
    mutex_t change_mutex;
    static void call(boost::function<void()>);
    publisher_controller_t<boost::function<void()> > change_publisher;

    static void write_metadata(std::ostream&, metadata_t);
    static void write_ping(std::ostream&, int);
    static void write_ping_response(std::ostream&, int);
    void on_utility_message(peer_id_t, std::istream&, const boost::function<void()> &);
    void on_connect(peer_id_t);
    void on_disconnect(peer_id_t);

    connectivity_service_t::peers_list_subscription_t event_watcher;

    int ping_id_counter;
    std::map<int, cond_t *> ping_waiters;
};

#include "rpc/metadata/metadata.tcc"

#endif /* __RPC_METADATA_METADATA_HPP__ */