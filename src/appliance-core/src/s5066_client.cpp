#include "s5066_client.h"

#include "frame.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
    if (!bcast) {
        auto it = peers_.find(f.dst);
        if (it == peers_.end()) {
            // Fails through on_tx_done, never from inside send().
            sending_ = true;
            pending_fail_ = "no 5066 address for " + f.dst + " in the peers file";
            return true;
        }
        addr = it->second;
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
    for (auto& p : pieces) out += s5066::unidata_request(prio, cfg_.sap, addr, st, ttl, p);
    if (!write_all(out)) return false;

    sending_ = true;
    if (bcast) {
        // Nothing comes back for a non-ARQ broadcast: it is sent once the
        // node has it.
        pending_ok_ = true;
        outstanding_ = 0;
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
