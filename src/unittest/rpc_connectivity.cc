#include "unittest/gtest.hpp"

#include "unittest_utils.hpp"
#include "rpc/connectivity/cluster.hpp"

namespace unittest {

struct starter_t : public thread_message_t {
    thread_pool_t *tp;
    boost::function<void()> fun;
    starter_t(thread_pool_t *tp, boost::function<void()> fun) : tp(tp), fun(fun) { }
    void run() {
        fun();
        tp->shutdown();
    }
    void on_thread_switch() {
        coro_t::spawn_now(boost::bind(&starter_t::run, this));
    }
};

/* `let_stuff_happen()` delays for some time to let events occur */
void let_stuff_happen() {
    nap(1000);
}

/* `recording_test_application_t` sends and receives integers over a `message_service_t`.
It keeps track of the integers it has received. */

class recording_test_application_t : public home_thread_mixin_t {
public:
    explicit recording_test_application_t(message_service_t *s) :
        service(s),
        sequence_number(0),
        message_handler_registration(s, boost::bind(&recording_test_application_t::on_message, this, _1, _2))
        { }
    void send(int message, peer_id_t peer) {
        service->send_message(peer, boost::bind(&write, message, _1));
    }
    void expect(int message, peer_id_t peer) {
        expect_delivered(message);
        assert_thread();
        EXPECT_TRUE(inbox[message] == peer);
    }
    void expect_delivered(int message) {
        assert_thread();
        EXPECT_TRUE(inbox.find(message) != inbox.end());
    }
    void expect_undelivered(int message) {
        assert_thread();
        EXPECT_TRUE(inbox.find(message) == inbox.end());
    }
    void expect_order(int first, int second) {
        expect_delivered(first);
        expect_delivered(second);
        assert_thread();
        EXPECT_LT(timing[first], timing[second]);
    }

private:
    void on_message(peer_id_t peer, std::istream &stream) {
        int i;
        stream >> i;
        on_thread_t th(home_thread());
        inbox[i] = peer;
        timing[i] = sequence_number++;
    }
    static void write(int i, std::ostream &stream) {
        stream << i;
    }

    message_service_t *service;
    std::map<int, peer_id_t> inbox;
    std::map<int, int> timing;
    int sequence_number;
    message_service_t::handler_registration_t message_handler_registration;
};

/* `StartStop` starts a cluster of three nodes, then shuts it down again. */

void run_start_stop_test() {
    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port), c2(port+1), c3(port+2);
    c2.join(peer_address_t(ip_address_t::us(), port));
    c3.join(peer_address_t(ip_address_t::us(), port));
    let_stuff_happen();
}
TEST(RPCConnectivityTest, StartStop) {
    run_in_thread_pool(&run_start_stop_test);
}

TEST(RPCConnectivityTest, StartStopMultiThread) {
    run_in_thread_pool(&run_start_stop_test, 3);
}


/* `Message` sends some simple messages between the nodes of a cluster. */

void run_message_test() {
    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port), c2(port+1), c3(port+2);
    recording_test_application_t a1(&c1), a2(&c2), a3(&c3);
    c2.join(peer_address_t(ip_address_t::us(), port));
    c3.join(peer_address_t(ip_address_t::us(), port));

    let_stuff_happen();

    a1.send(873, c2.get_me());
    a2.send(66663, c1.get_me());
    a3.send(6849, c1.get_me());
    a3.send(999, c3.get_me());

    let_stuff_happen();

    a2.expect(873, c1.get_me());
    a1.expect(66663, c2.get_me());
    a1.expect(6849, c3.get_me());
    a3.expect(999, c3.get_me());
}
TEST(RPCConnectivityTest, Message) {
    run_in_thread_pool(&run_message_test);
}
TEST(RPCConnectivityTest, MesssageMultiThread) {
    run_in_thread_pool(&run_message_test, 3);
}

/* `UnreachablePeer` tests that messages sent to unreachable peers silently
fail. */

void run_unreachable_peer_test() {
    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port), c2(port+1);
    recording_test_application_t a1(&c1), a2(&c2);
    
    /* Note that we DON'T join them together. */

    let_stuff_happen();

    a1.send(888, c2.get_me());

    let_stuff_happen();

    /* The message should not have been delivered. The system shouldn't have
    crashed, either. */
    a2.expect_undelivered(888);

    c1.join(peer_address_t(ip_address_t::us(), port+1));

    let_stuff_happen();

    a1.send(999, c2.get_me());

    let_stuff_happen();

    a2.expect_undelivered(888);
    a2.expect(999, c1.get_me());
}
TEST(RPCConnectivityTest, UnreachablePeer) {
    run_in_thread_pool(&run_unreachable_peer_test);
}
TEST(RPCConnectivityTest, UnreachablePeerMultiThread) {
    run_in_thread_pool(&run_unreachable_peer_test, 3);
}

/* `Ordering` tests that messages sent by the same route arrive in the same
order they were sent in. */

void run_ordering_test() {
    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port), c2(port+1);
    recording_test_application_t a1(&c1), a2(&c2);

    c1.join(peer_address_t(ip_address_t::us(), port+1));

    let_stuff_happen();

    for (int i = 0; i < 10; i++) {
        a1.send(i, c2.get_me());
        a1.send(i, c1.get_me());
    }

    let_stuff_happen();

    for (int i = 0; i < 9; i++) {
        a1.expect_order(i, i+1);
        a2.expect_order(i, i+1);
    }
}
TEST(RPCConnectivityTest, Ordering) {
    run_in_thread_pool(&run_ordering_test);
}
TEST(RPCConnectivityTest, OrderingMultiThread) {
    run_in_thread_pool(&run_ordering_test, 3);
}

/* `GetPeersList` confirms that the behavior of `cluster_t::get_peers_list()` is
correct. */

void run_get_peers_list_test() {
    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port);

    /* Make sure `get_peers_list()` is initially sane */
    std::set<peer_id_t> list_1 = c1.get_peers_list();
    EXPECT_TRUE(list_1.find(c1.get_me()) != list_1.end());
    EXPECT_EQ(list_1.size(), 1);

    {
        connectivity_cluster_t c2(port+1);
        c2.join(peer_address_t(ip_address_t::us(), port));

        let_stuff_happen();

        /* Make sure `get_peers_list()` correctly notices that a peer connects */
        std::set<peer_id_t> list_2 = c1.get_peers_list();
        EXPECT_TRUE(list_2.find(c2.get_me()) != list_2.end());
        EXPECT_EQ(port + 1, c1.get_peer_address(c2.get_me()).port);

        /* `c2`'s destructor is called here */
    }

    let_stuff_happen();

    /* Make sure `get_peers_list()` notices that a peer has disconnected */
    std::set<peer_id_t> list_3 = c1.get_peers_list();
    EXPECT_EQ(list_3.size(), 1);
}
TEST(RPCConnectivityTest, GetPeersList) {
    run_in_thread_pool(&run_get_peers_list_test);
}
TEST(RPCConnectivityTest, GetPeersListMultiThread) {
    run_in_thread_pool(&run_get_peers_list_test, 3);
}

/* `EventWatchers` confirms that `disconnect_watcher_t` and
`connectivity_service_t::peers_list_subscription_t` work properly. */

void run_event_watchers_test() {
    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port);

    boost::scoped_ptr<connectivity_cluster_t> c2(new connectivity_cluster_t(port+1));
    peer_id_t c2_id = c2->get_me();

    /* Make sure `c1` notifies us when `c2` connects */
    cond_t connection_established;
    connectivity_service_t::peers_list_subscription_t subs(
        boost::bind(&cond_t::pulse, &connection_established),
        NULL);
    {
        connectivity_service_t::peers_list_freeze_t freeze(&c1);
        if (c1.get_peers_list().count(c2->get_me()) == 0) {
            subs.reset(&c1, &freeze);
        } else {
            connection_established.pulse();
        }
    }
    
    EXPECT_FALSE(connection_established.is_pulsed());
    c1.join(peer_address_t(ip_address_t::us(), port+1));
    let_stuff_happen();
    EXPECT_TRUE(connection_established.is_pulsed());

    /* Make sure `c1` notifies us when `c2` disconnects */
    disconnect_watcher_t disconnect_watcher(&c1, c2_id);
    EXPECT_FALSE(disconnect_watcher.is_pulsed());
    c2.reset();
    let_stuff_happen();
    EXPECT_TRUE(disconnect_watcher.is_pulsed());

    /* Make sure `disconnect_watcher_t` works for an already-unconnected peer */
    disconnect_watcher_t disconnect_watcher_2(&c1, c2_id);
    EXPECT_TRUE(disconnect_watcher_2.is_pulsed());
}
TEST(RPCConnectivityTest, EventWatchers) {
    run_in_thread_pool(&run_event_watchers_test);
}
TEST(RPCConnectivityTest, EventWatchersMultiThread) {
    run_in_thread_pool(&run_event_watchers_test, 3);
}

/* `EventWatcherOrdering` confirms that information delivered via event
notification is consistent with information delivered via `get_peers_list()`. */

struct watcher_t {

    explicit watcher_t(connectivity_cluster_t *c, recording_test_application_t *a) :
        cluster(c),
        application(a),
        event_watcher(
            boost::bind(&watcher_t::on_connect, this, _1),
            boost::bind(&watcher_t::on_disconnect, this, _1))
    {
        connectivity_service_t::peers_list_freeze_t freeze(cluster);
        event_watcher.reset(cluster, &freeze);
    }

    void on_connect(peer_id_t p) {
        /* When we get a connection event, make sure that the peer address
        is present in the routing table */
        std::set<peer_id_t> list = cluster->get_peers_list();
        EXPECT_TRUE(list.find(p) != list.end());

        /* Make sure messages sent from connection events are delivered
        properly. We must use `coro_t::spawn_now()` because `send_message()`
        may block. */
        coro_t::spawn_now(boost::bind(&recording_test_application_t::send, application, 89765, p));
    }

    void on_disconnect(peer_id_t p) {
        /* When we get a disconnection event, make sure that the peer
        address is gone from the routing table */
        std::set<peer_id_t> list = cluster->get_peers_list();
        EXPECT_TRUE(list.find(p) == list.end());
    }

    connectivity_cluster_t *cluster;
    recording_test_application_t *application;
    connectivity_service_t::peers_list_subscription_t event_watcher;
};

void run_event_watcher_ordering_test() {

    int port = 10000 + rand() % 20000;
    connectivity_cluster_t c1(port);
    recording_test_application_t a1(&c1);

    watcher_t watcher(&c1, &a1);

    /* Generate some connection/disconnection activity */
    {
        connectivity_cluster_t c2(port+1);
        recording_test_application_t a2(&c2);
        c2.join(peer_address_t(ip_address_t::us(), port));

        let_stuff_happen();

        /* Make sure that the message sent in `on_connect()` was delivered */
        a2.expect(89765, c1.get_me());
    }

    let_stuff_happen();
}
TEST(RPCConnectivityTest, EventWatcherOrdering) {
    run_in_thread_pool(&run_event_watcher_ordering_test);
}
TEST(RPCConnectivityTest, EventWatcherOrderingMultiThread) {
    run_in_thread_pool(&run_event_watcher_ordering_test, 3);
}

/* `StopMidJoin` makes sure that nothing breaks if you shut down the cluster
while it is still coming up */

void run_stop_mid_join_test() {

    int port = 10000 + rand() % 20000;

    const int num_members = 5;

    /* Spin up 20 cluster-members */
    boost::scoped_ptr<connectivity_cluster_t> nodes[num_members];
    for (int i = 0; i < num_members; i++) {
        nodes[i].reset(new connectivity_cluster_t(port+i));
    }
    for (int i = 1; i < num_members; i++) {
        nodes[i]->join(peer_address_t(ip_address_t::us(), port));
    }

    coro_t::yield();

    EXPECT_NE(nodes[1]->get_peers_list().size(), num_members) << "This test is "
        "supposed to test what happens when a cluster is interrupted as it "
        "starts up, but the cluster finished starting up before we could "
        "interrupt it.";

    /* Shut down cluster nodes and hope nothing crashes. (The destructors do the
    work of shutting down.) */
}
TEST(RPCConnectivityTest, StopMidJoin) {
    run_in_thread_pool(&run_stop_mid_join_test);
}
TEST(RPCConnectivityTest, StopMidJoinMultiThread) {
    run_in_thread_pool(&run_stop_mid_join_test, 3);
}

/* `BlobJoin` tests whether two groups of cluster nodes can correctly merge
together. */

void run_blob_join_test() {

    int port = 10000 + rand() % 20000;

    /* Two blobs of `blob_size` nodes */
    const int blob_size = 4;

    /* Spin up cluster-members */
    boost::scoped_ptr<connectivity_cluster_t> nodes[blob_size * 2];
    for (int i = 0; i < blob_size * 2; i++) {
        nodes[i].reset(new connectivity_cluster_t(port+i));
    }

    for (int i = 1; i < blob_size; i++) {
        nodes[i]->join(peer_address_t(ip_address_t::us(), port));
    }
    for (int i = blob_size+1; i < blob_size*2; i++) {
        nodes[i]->join(peer_address_t(ip_address_t::us(), port+blob_size));
    }

    let_stuff_happen();

    nodes[1]->join(peer_address_t(ip_address_t::us(), port+blob_size+1));

    let_stuff_happen();
    let_stuff_happen();
    let_stuff_happen();

    /* Make sure every node sees every other */
    for (int i = 0; i < blob_size*2; i++) {
        ASSERT_EQ(blob_size * 2, nodes[i]->get_peers_list().size());
    }
}
TEST(RPCConnectivityTest, BlobJoin) {
    run_in_thread_pool(&run_blob_join_test);
}
TEST(RPCConnectivityTest, BlobJoinMultiThread) {
    run_in_thread_pool(&run_blob_join_test, 3);
}

/* `BinaryData` makes sure that any octet can be sent over the wire. */

class binary_test_application_t {
public:
    explicit binary_test_application_t(message_service_t *s) :
        service(s),
        got_spectrum(false),
        message_handler_registration(s, boost::bind(&binary_test_application_t::on_message, this, _1, _2))
        { }
    static void dump_spectrum(std::ostream &stream) {
        char spectrum[CHAR_MAX - CHAR_MIN + 1];
        for (int i = CHAR_MIN; i <= CHAR_MAX; i++) spectrum[i - CHAR_MIN] = i;
        stream.write(spectrum, CHAR_MAX - CHAR_MIN + 1);
    }
    void send_spectrum(peer_id_t peer) {
        service->send_message(peer, &dump_spectrum);
    }
    void on_message(peer_id_t, std::istream &stream) {
        char spectrum[CHAR_MAX - CHAR_MIN + 1];
        stream.read(spectrum, CHAR_MAX - CHAR_MIN + 1);
        int eof = stream.peek();
        for (int i = CHAR_MIN; i <= CHAR_MAX; i++) {
            EXPECT_EQ(spectrum[i - CHAR_MIN], i);
        }
        EXPECT_EQ(eof, EOF);
        got_spectrum = true;
    }
    message_service_t *service;
    bool got_spectrum;
    message_service_t::handler_registration_t message_handler_registration;
};

void run_binary_data_test() {

    int port = 10000 + rand() % 20000;
    connectivity_cluster_t cluster1(port), cluster2(port+1);
    binary_test_application_t application1(&cluster1), application2(&cluster2);
    cluster1.join(cluster2.get_peer_address(cluster2.get_me()));

    let_stuff_happen();

    application1.send_spectrum(cluster2.get_me());

    let_stuff_happen();

    EXPECT_TRUE(application2.got_spectrum);
}
TEST(RPCConnectivityTest, BinaryData) {
    run_in_thread_pool(&run_binary_data_test);
}
TEST(RPCConnectivityTest, BinaryDataMultiThread) {
    run_in_thread_pool(&run_binary_data_test, 3);
}

/* `PeerIDSemantics` makes sure that `peer_id_t::is_nil()` works as expected. */

void run_peer_id_semantics_test() {

    peer_id_t nil_peer;
    ASSERT_TRUE(nil_peer.is_nil());

    int port = 10000 + rand() % 20000;
    connectivity_cluster_t cluster_node(port);
    ASSERT_FALSE(cluster_node.get_me().is_nil());
}
TEST(RPCConnectivityTest, PeerIDSemantics) {
    run_in_thread_pool(&run_peer_id_semantics_test);
}
TEST(RPCConnectivityTest, PeerIDSemanticsMultiThread) {
    run_in_thread_pool(&run_peer_id_semantics_test, 3);
}

}   /* namespace unittest */