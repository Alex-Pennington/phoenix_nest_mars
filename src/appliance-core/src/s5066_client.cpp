#include "s5066_client.h"

#include "frame.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace pna {

namespace {

long long mtime_of(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
}

}  // namespace

S5066Client::S5066Client(S5066Config cfg) : cfg_(std::move(cfg)) {}

S5066Client::~S5066Client() { stop(); }

void S5066Client::event(const std::string& text) {
    if (cb_.on_event) cb_.on_event(text);
}

size_t S5066Client::peers() const {
    std::lock_guard<std::mutex> g(mu_);
    return peers_.size();
}

std::string S5066Client::describe() const {
    std::string s = "STANAG 5066 " + cfg_.host + ":" + std::to_string(cfg_.port) + " SAP " + std::to_string(cfg_.sap);
    if (bound_) s += ", MTU " + std::to_string(mtu_.load());
    else s += ", not bound";
    return s;
}

void S5066Client::start(Callbacks cb) {
    if (running_) return;
    cb_ = std::move(cb);
    {
        std::lock_guard<std::mutex> g(mu_);
        reload_peers_if_changed();
    }
    running_ = true;
    worker_ = std::thread([this] { run(); });
}

void S5066Client::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> g(mu_);
    if (fd_ >= 0) {
        std::string bye = s5066::unbind_request();
        (void)::send(fd_, bye.data(), bye.size(), MSG_NOSIGNAL);
        ::close(fd_);
        fd_ = -1;
    }
    bound_ = false;
}

// Caller holds mu_.
void S5066Client::reload_peers_if_changed() {
    if (cfg_.peers_file.empty()) return;
    long long m = mtime_of(cfg_.peers_file);
    if (m == peers_mtime_) return;
    peers_mtime_ = m;
    std::map<std::string, uint32_t> fresh;
    std::string err;
    if (m < 0 || !s5066::read_peers(cfg_.peers_file, fresh, &err)) {
        notes_.push_back("5066 peers not read (" + (m < 0 ? "can't read " + cfg_.peers_file : err) +
                         "); keeping " + std::to_string(peers_.size()));
        return;
    }
    peers_.swap(fresh);
    notes_.push_back("5066 peers: " + std::to_string(peers_.size()) + " stations");
}

// Caller holds mu_.
bool S5066Client::write_all(const std::string& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = ::send(fd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool S5066Client::send(const std::string& bytes) {
    std::lock_guard<std::mutex> g(mu_);
    if (!bound_ || sending_ || fd_ < 0) return false;
    reload_peers_if_changed();

    // The frame's own header says where it goes and how urgent it is.
    FrameParser fp;
    auto frames = fp.feed(bytes);
    if (frames.size() != 1) return false;
    const Frame& f = frames.front();

    bool bcast = is_broadcast(f.dst);
    uint32_t addr = s5066::kBroadcastAddress;
    std::vector<uint32_t> dests;
    if (bcast) {
        if (cfg_.broadcast_each_peer) {
            for (auto& kv : peers_)
                if (kv.first != f.src) dests.push_back(kv.second);
            if (dests.empty()) {
                sending_ = true;
                pending_fail_ = "no stations in the peers file to send to all";
                return true;
            }
        } else {
            dests.push_back(s5066::kBroadcastAddress);
        }
    } else {
        auto it = peers_.find(f.dst);
        if (it == peers_.end()) {
            // Fails through on_tx_done, never from inside send().
            sending_ = true;
            pending_fail_ = "no 5066 address for " + f.dst + " in the peers file";
            return true;
        }
        addr = it->second;
        dests.push_back(addr);
    }

    auto pieces = s5066::split(bytes, ++seq_, mtu_);
    if (pieces.empty()) return false;

    s5066::ServiceType st;
    uint32_t ttl = cfg_.ttl_seconds;
    if (bcast) {
        st.tx_mode = s5066::kNonArqBroadcast;
        st.confirm = s5066::kNoConfirm;
        st.retries = cfg_.broadcast_retries;
    } else {
        st.tx_mode = s5066::kArq;
        st.confirm = cfg_.confirm;
    }
    uint8_t prio = s5066::priority_for(f.prec);
    std::string out;
    for (uint32_t d : dests)
        for (auto& p : pieces) out += s5066::unidata_request(prio, cfg_.sap, d, st, ttl, p);
    if (!write_all(out)) return false;

    sending_ = true;
    if (bcast) {
        // Nothing comes back for a non-ARQ copy but a refusal, and the node
        // refuses at once: give it a moment, then call the broadcast sent.
        outstanding_ = 0;
        bcast_copies_ = static_cast<int>(dests.size() * pieces.size());
        bcast_refused_ = 0;
        bcast_why_.clear();
        bcast_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    } else {
        outstanding_ = static_cast<int>(pieces.size());
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(ttl ? ttl + 30 : 24 * 3600);
    }
    return true;
}

// Caller holds mu_.
void S5066Client::finish_send(bool ok, const std::string& err, std::vector<Action>& after) {
    if (!sending_) return;
    sending_ = false;
    outstanding_ = 0;
    pending_ok_ = false;
    pending_fail_.clear();
    bcast_copies_ = 0;
    after.push_back([this, ok, err] {
        if (cb_.on_tx_done) cb_.on_tx_done(ok, err);
    });
}

// Caller holds mu_.
void S5066Client::close_link(const std::string& why, std::vector<Action>& after) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    bool was = bound_.exchange(false);
    flow_off_ = false;
    reader_ = s5066::Reader();
    if (sending_) finish_send(false, "5066 node connection lost (" + why + ")", after);
    if (was) after.push_back([this, why] { event("5066 node: " + why); });
}

bool S5066Client::connect_and_bind() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(cfg_.port);
    if (::inet_pton(AF_INET, cfg_.host.c_str(), &sa.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) {
        ::close(fd);
        return false;
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    s5066::ServiceType st;
    st.tx_mode = s5066::kArq;
    st.confirm = cfg_.confirm;
    std::lock_guard<std::mutex> g(mu_);
    fd_ = fd;
    reader_ = s5066::Reader();
    if (!write_all(s5066::bind_request(cfg_.sap, cfg_.rank, st))) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

// Caller holds mu_.
void S5066Client::handle(const s5066::Prim& p, std::vector<Action>& after) {
    using namespace s5066;
    switch (p.type) {
    case kBindAccepted:
        mtu_ = p.mtu;
        bound_ = true;
        after.push_back([this] { event("5066 node: bound, " + describe()); });
        break;
    case kBindRejected: {
        std::string why = "bind refused: " + bind_rejected_text(p.reason);
        close_link(why, after);
        after.push_back([this, why] { event("5066 node: " + why); });
        break;
    }
    case kUnbindIndication:
        close_link("unbound by the node: " + unbind_text(p.reason), after);
        break;
    case kKeepAlive:
        write_all(keep_alive());
        break;
    case kDataflowOff:
        flow_off_ = true;
        after.push_back([this] { event("5066 node: data flow off, sends held"); });
        break;
    case kDataflowOn:
        flow_off_ = false;
        after.push_back([this] { event("5066 node: data flow on"); });
        break;
    case kSubnetAvailability: {
        static const char* names[] = {"off", "on", "receive only", "half duplex", "full duplex"};
        std::string s = p.status < 5 ? names[p.status] : "status " + std::to_string(p.status);
        after.push_back([this, s] { event("5066 subnetwork " + s); });
        break;
    }
    case kUnidataRequestConfirm:
        if (sending_ && outstanding_ > 0 && --outstanding_ == 0) finish_send(true, "", after);
        break;
    case kUnidataRequestRejected:
        if (sending_ && bcast_copies_ > 0) {
            ++bcast_refused_;
            bcast_why_ = unidata_rejected_text(p.reason);
            uint32_t d = p.dst;
            after.push_back([this, d, why = bcast_why_] {
                event("5066 node refused the copy for " + s5066::address_text(d) + ": " + why);
            });
            break;
        }
        if (sending_ && outstanding_ > 0)
            finish_send(false, "5066 node refused the send: " + unidata_rejected_text(p.reason), after);
        break;
    case kUnidataIndication:
        if (p.sap != cfg_.sap) break;
        for (auto& whole : joiner_.add(p.src, p.data))
            after.push_back([this, whole] {
                if (cb_.on_rx) cb_.on_rx(whole);
            });
        break;
    default:
        break;
    }
}

void S5066Client::run() {
    auto next_try = std::chrono::steady_clock::now();
    bool told_down = false;
    while (running_) {
        std::vector<Action> after;
        int fd;
        {
            std::lock_guard<std::mutex> g(mu_);
            fd = fd_;
            for (auto& n : notes_) after.push_back([this, n] { event(n); });
            notes_.clear();
            // A send that failed before reaching the node, or a broadcast done.
            if (sending_ && !pending_fail_.empty()) finish_send(false, pending_fail_, after);
            else if (sending_ && pending_ok_) finish_send(true, "", after);
            else if (sending_ && bcast_copies_ > 0 && std::chrono::steady_clock::now() > bcast_until_) {
                if (bcast_refused_ >= bcast_copies_)
                    finish_send(false, "5066 node refused every copy: " + bcast_why_, after);
                else
                    finish_send(true, "", after);
            }
            else if (sending_ && outstanding_ > 0 && std::chrono::steady_clock::now() > deadline_)
                finish_send(false, "no confirm from the 5066 node in time", after);
        }
        for (auto& a : after) a();
        after.clear();

        if (fd < 0) {
            if (std::chrono::steady_clock::now() >= next_try) {
                if (!connect_and_bind()) {
                    if (!told_down) event("5066 node not reachable at " + cfg_.host + ":" + std::to_string(cfg_.port) +
                                          "; trying every " + std::to_string(cfg_.reconnect_ms / 1000) + " s");
                    told_down = true;
                    next_try = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
                } else {
                    told_down = false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_ms));
            continue;
        }

        pollfd pfd{fd, POLLIN, 0};
        int r = ::poll(&pfd, 1, cfg_.poll_ms);
        if (r <= 0) continue;
        char buf[4096];
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        std::lock_guard<std::mutex> g(mu_);
        if (fd_ != fd) continue;
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            close_link("connection closed", after);
            next_try = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
        } else {
            for (auto& raw : reader_.feed(std::string(buf, static_cast<size_t>(n)))) {
                s5066::Prim p;
                if (s5066::parse(raw, p)) handle(p, after);
                if (fd_ < 0) {
                    next_try = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
                    break;
                }
            }
        }
        // Run the callbacks without our lock: the core takes its own.
        mu_.unlock();
        for (auto& a : after) a();
        mu_.lock();
    }
}

}  // namespace pna

// ---- link = controller ----------------------------------------------------

namespace pna {

namespace {

// Figure A-3 and Figure A-29: Transmission Mode 0010 (non-ARQ), Delivery
// Confirmation 00 (none), Delivery Order 0 (in order), no retransmission.
s5066::ServiceType controller_service_type() {
    s5066::ServiceType st;
    st.tx_mode = s5066::kNonArqBroadcast;
    st.confirm = s5066::kNoConfirm;
    st.as_they_arrive = false;
    st.retries = 0;
    return st;
}

bool write_fd(int fd, const std::string& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = ::send(fd, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

std::string sender_name(bool automatic) { return automatic ? "automatic sender" : "operator's sender"; }

}  // namespace

SisControllerLink::SisControllerLink(SisLinkConfig cfg) : cfg_(std::move(cfg)) {}

SisControllerLink::~SisControllerLink() { stop(); }

std::string SisControllerLink::path_for(const std::string& service) const {
    return cfg_.dir + "/sis-" + service + ".sock";
}

void SisControllerLink::add(const std::string& service, bool automatic) {
    std::lock_guard<std::mutex> g(mu_);
    if (find(service, automatic)) return;
    auto c = std::make_unique<Conn>();
    c->service = service;
    c->automatic = automatic;
    c->sap = automatic ? cfg_.sap_automatic : cfg_.sap_operator;
    conns_.push_back(std::move(c));
}

SisControllerLink::Conn* SisControllerLink::find(const std::string& service, bool automatic) {
    for (auto& c : conns_)
        if (c->service == service && c->automatic == automatic) return c.get();
    return nullptr;
}

const SisControllerLink::Conn* SisControllerLink::find(const std::string& service, bool automatic) const {
    for (auto& c : conns_)
        if (c->service == service && c->automatic == automatic) return c.get();
    return nullptr;
}

bool SisControllerLink::has(const std::string& service, bool automatic) const {
    std::lock_guard<std::mutex> g(mu_);
    return find(service, automatic) != nullptr;
}

uint16_t SisControllerLink::bound_mtu(const std::string& service, bool automatic) const {
    std::lock_guard<std::mutex> g(mu_);
    const Conn* c = find(service, automatic);
    return (c && c->bound) ? c->mtu : 0;
}

bool SisControllerLink::bound(const std::string& service, bool automatic) const {
    std::lock_guard<std::mutex> g(mu_);
    const Conn* c = find(service, automatic);
    return c && c->bound;
}

size_t SisControllerLink::outstanding() const {
    std::lock_guard<std::mutex> g(mu_);
    size_t n = 0;
    for (auto& c : conns_) n += c->pending.size();
    return n;
}

void SisControllerLink::start(Result on_result, std::function<void(const std::string&)> log, Bound on_bound) {
    if (running_) return;
    on_result_ = std::move(on_result);
    log_ = std::move(log);
    on_bound_ = std::move(on_bound);
    running_ = true;
    worker_ = std::thread([this] { run(); });
}

void SisControllerLink::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> g(mu_);
    for (auto& c : conns_) {
        if (c->fd >= 0) {
            // S_UNBIND_REQUEST (A.2.1.2): the controller drops what it holds unkeyed.
            (void)write_fd(c->fd, s5066::unbind_request());
            ::close(c->fd);
        }
        c->fd = -1;
        c->bound = false;
        c->pending.clear();  // nothing is told to a core going away
        c->late.clear();
    }
}

bool SisControllerLink::submit(const SisSend& s, std::string* why) {
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    std::lock_guard<std::mutex> g(mu_);
    Conn* c = find(s.service, s.automatic);
    if (!c)
        return fail("no SIS " + sender_name(s.automatic) + " for the " + s.service + " service (" +
                    (s.automatic ? "it has no automatic modes" : "its table does not transmit") + ")");
    if (!c->bound || c->fd < 0)
        return fail("the station controller isn't bound for " + s.service + " on SAP " + std::to_string(c->sap) +
                    " (" + path_for(s.service) + ")");
    if (s.frame.size() > c->mtu)
        return fail("too long for one burst: " + std::to_string(s.frame.size()) +
                    " bytes, the station controller takes " + std::to_string(c->mtu));
    // A.2.1.6: priority, destination SAP (the same client application at the
    // far station), the all-stations address, delivery mode, TTL, the U_PDU.
    const std::string req = s5066::unidata_request(s.priority & 0x0F, c->sap, s5066::kBroadcastAddress,
                                                   controller_service_type(), cfg_.ttl_seconds, s.frame);
    if (!write_fd(c->fd, req)) return fail("the request didn't reach the station controller");
    Pending p;
    p.send = s;
    // A TTL of 0 is no limit of its own (A.2.1.6 (8)): the answer is waited for
    // until it comes or the connection drops.
    p.deadline = cfg_.ttl_seconds == 0
                     ? std::chrono::steady_clock::time_point::max()
                     : std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.ttl_seconds + cfg_.answer_grace_s);
    c->pending.push_back(std::move(p));
    return true;
}

// Caller holds mu_.
void SisControllerLink::open(Conn& c, std::vector<std::string>& notes) {
    const std::string path = path_for(c.service);
    sockaddr_un a{};
    auto said = [&](const std::string& w) {
        if (c.told != w) notes.push_back(w);
        c.told = w;
    };
    if (path.size() >= sizeof a.sun_path) {
        said("SIS " + c.service + ": the socket path is too long for a Unix socket: " + path);
        return;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.c_str(), path.size());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        ::close(fd);
        said("SIS " + c.service + " SAP " + std::to_string(c.sap) + ": the station controller isn't answering on " +
             path + "; nothing of " + c.service + " is handed down until it is");
        return;
    }
    // A.2.1.1: S_BIND_REQUEST(SAP ID, rank, service type). Rank 0.
    if (!write_fd(fd, s5066::bind_request(c.sap, 0, controller_service_type()))) {
        ::close(fd);
        return;
    }
    c.fd = fd;
    c.bound = false;
    c.reader = s5066::Reader();
}

// Caller holds mu_. Every request without its answer ends unanswered.
void SisControllerLink::drop(Conn& c, const std::string& why, std::vector<Done>& done,
                             std::vector<std::string>& notes) {
    if (c.fd >= 0) ::close(c.fd);
    c.fd = -1;
    const bool was = c.bound;
    c.bound = false;
    c.next_try = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
    for (auto& p : c.pending) done.push_back(Done{p.send, End::unanswered, 0, why});
    c.pending.clear();
    c.late.clear();  // an answer comes only on the connection that asked
    if (was) {
        const std::string w = "SIS " + c.service + " SAP " + std::to_string(c.sap) + ": " + why;
        notes.push_back(w);
        c.told = w;
    }
}

// Caller holds mu_.
void SisControllerLink::handle(Conn& c, const s5066::Prim& p, std::vector<Done>& done,
                               std::vector<std::string>& notes) {
    switch (p.type) {
    case s5066::kBindAccepted:
        // A.2.1.3: the SAP is ours; the MTU bounds every U_PDU after it.
        c.bound = true;
        c.mtu = p.mtu;
        c.told = "bound";
        notes.push_back("SIS " + c.service + ": the " + sender_name(c.automatic) + " is bound on SAP " +
                        std::to_string(c.sap) + ", MTU " + std::to_string(p.mtu));
        bound_now_.emplace_back(c.service, c.automatic);
        return;
    case s5066::kBindRejected:
        // A.2.1.4: try again later; the reason is said once.
    {
        const std::string w = "SIS " + c.service + " SAP " + std::to_string(c.sap) + ": bind rejected: " +
                              s5066::bind_rejected_text(p.reason);
        drop(c, "bind rejected: " + s5066::bind_rejected_text(p.reason), done, notes);
        if (c.told != w) notes.push_back(w);
        c.told = w;
        return;
    }
    case s5066::kUnbindIndication:
        drop(c, "the station controller unbound it: " + s5066::unbind_text(p.reason), done, notes);
        return;
    case s5066::kKeepAlive:
        (void)write_fd(c.fd, s5066::keep_alive());  // A.2.1.17
        return;
    case s5066::kUnidataRequestConfirm:
    case s5066::kUnidataRequestRejected: {
        // A.2.1.7 (7): the copy of the U_PDU (or its first bytes) names the
        // request; an empty copy is the oldest one. The copy can't tell two
        // requests of the same bytes apart, so the oldest is taken: the late
        // list first (every request on it is older than any waiting), then
        // the requests waiting.
        auto match = [&p](std::deque<Pending>& q) {
            auto it = q.begin();
            if (!p.data.empty())
                for (; it != q.end(); ++it)
                    if (it->send.frame.compare(0, p.data.size(), p.data) == 0) break;
            return it;
        };
        const bool late = match(c.late) != c.late.end();
        std::deque<Pending>& q = late ? c.late : c.pending;
        auto it = match(q);
        if (it == q.end()) {
            notes.push_back("SIS " + c.service + ": an answer for no request waiting; ignored");
            return;
        }
        Done d{it->send, p.type == s5066::kUnidataRequestConfirm ? End::confirmed : End::rejected, p.reason, "",
               late};
        if (d.end == End::rejected) d.why = s5066::unidata_rejected_text(p.reason);
        q.erase(it);
        done.push_back(std::move(d));
        return;
    }
    default:
        return;  // nothing else is ours to read on this link
    }
}

void SisControllerLink::run() {
    while (running_) {
        std::vector<Done> done;
        std::vector<std::string> notes;
        std::vector<pollfd> fds;
        std::vector<Conn*> who;
        std::vector<std::pair<std::string, bool>> bound;
        {
            std::lock_guard<std::mutex> g(mu_);
            const auto now = std::chrono::steady_clock::now();
            for (auto& cp : conns_) {
                Conn& c = *cp;
                if (c.fd < 0 && now >= c.next_try) {
                    open(c, notes);
                    if (c.fd < 0) c.next_try = now + std::chrono::milliseconds(cfg_.reconnect_ms);
                }
                // The time to live plus the grace, and no answer: unanswered.
                while (!c.pending.empty() && now >= c.pending.front().deadline) {
                    done.push_back(Done{c.pending.front().send, End::unanswered, 0,
                                        "no answer from the station controller within " +
                                            std::to_string(cfg_.ttl_seconds + cfg_.answer_grace_s) + " s"});
                    // Its answer may still come: kept, so it is not taken for another's.
                    Pending l = std::move(c.pending.front());
                    c.pending.pop_front();
                    l.deadline = now + std::chrono::seconds(cfg_.late_keep_s);
                    c.late.push_back(std::move(l));
                    if (c.late.size() > kSisLateMax) c.late.pop_front();
                }
                while (!c.late.empty() && now >= c.late.front().deadline) c.late.pop_front();
                if (c.fd >= 0) {
                    fds.push_back(pollfd{c.fd, POLLIN, 0});
                    who.push_back(&c);
                }
            }
        }
        if (!fds.empty()) ::poll(fds.data(), fds.size(), 100);
        else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            std::lock_guard<std::mutex> g(mu_);
            for (size_t i = 0; i < fds.size(); ++i) {
                Conn& c = *who[i];
                if (c.fd != fds[i].fd || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                char buf[8192];
                ssize_t n = ::recv(c.fd, buf, sizeof buf, MSG_DONTWAIT);
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                if (n <= 0) {
                    drop(c, "the connection to the station controller dropped", done, notes);
                    continue;
                }
                for (auto& raw : c.reader.feed(std::string(buf, static_cast<size_t>(n)))) {
                    s5066::Prim p;
                    if (s5066::parse(raw, p)) handle(c, p, done, notes);
                    if (c.fd < 0) break;
                }
            }
            bound.swap(bound_now_);
        }
        if (log_)
            for (auto& n : notes) log_(n);
        if (on_bound_)
            for (auto& b : bound) on_bound_(b.first, b.second);
        if (on_result_)
            for (auto& d : done) on_result_(d.send, d.end, d.reason, d.why, d.late);
    }
}

}  // namespace pna
