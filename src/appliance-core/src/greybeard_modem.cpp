#include "greybeard_modem.h"

#include "json.h"
#include "run_program.h"
#include "sha256.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace pna {

namespace {

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return static_cast<bool>(f) || f.eof();
}

const std::string kListing = ".modems.json";

bool ends_with(const std::string& s, const std::string& t) {
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

// A payload file named in a listing must sit beside it: a bare name, no path.
bool plain_name(const std::string& n) {
    return !n.empty() && n.find('/') == std::string::npos && n.find('\\') == std::string::npos && n != "." &&
           n != "..";
}

// The entries of a listing. The orchestrator gave the content (file, byte count,
// sha256) but not the field names, so a few likely spellings are accepted until
// docs/gale-term or the capture docs fix them.
std::vector<const JsonValue*> entries_of(const JsonValue& v) {
    std::vector<const JsonValue*> out;
    const JsonValue* list = nullptr;
    if (v.is_array()) list = &v;
    for (const char* k : {"verdicts", "files", "modems", "outputs", "payloads", "bursts"})
        if (!list) {
            const JsonValue* c = v.get(k);
            if (c && c->is_array()) list = c;
        }
    if (list)
        for (auto& e : list->arr)
            if (e.is_object()) out.push_back(&e);
    return out;
}

const JsonValue* first_of(const JsonValue& e, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        if (const JsonValue* v = e.get(k)) return v;
    return nullptr;
}

}  // namespace

GreybeardModem::GreybeardModem(GreybeardConfig cfg) : cfg_(std::move(cfg)) {}

GreybeardModem::~GreybeardModem() { stop(); }

void GreybeardModem::event(const std::string& text) {
    if (cb_.on_event) cb_.on_event(text);
}

void GreybeardModem::start(Callbacks cb) {
    cb_ = std::move(cb);
    for (const std::string& d : {cfg_.work_dir, cfg_.outbox_dir, cfg_.captures_dir})
        if (!d.empty()) fs::create_directories(d);

    // Bursts already handled. On a first start, every listing already in the
    // directory counts as handled: old traffic is not fed in again.
    bool first = cfg_.state_file.empty() || !fs::exists(cfg_.state_file);
    if (!first) {
        std::ifstream f(cfg_.state_file);
        for (std::string line; std::getline(f, line);)
            if (!line.empty()) seen_.insert(line);
    } else if (!cfg_.captures_dir.empty()) {
        std::error_code ec;
        for (auto& de : fs::directory_iterator(cfg_.captures_dir, ec)) {
            std::string n = de.path().filename().string();
            if (ends_with(n, kListing)) mark_seen(n);
        }
        if (!cfg_.state_file.empty()) std::ofstream(cfg_.state_file, std::ios::app);
    }

    running_ = true;
    tx_thread_ = std::thread([this] { tx_loop(); });
    if (!cfg_.captures_dir.empty()) rx_thread_ = std::thread([this] { rx_loop(); });
    event("greybeard-ale modem: " + describe());
}

void GreybeardModem::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (tx_thread_.joinable()) tx_thread_.join();
    if (rx_thread_.joinable()) rx_thread_.join();
}

std::string GreybeardModem::describe() const {
    return "greybeard-ale " + cfg_.waveform + " " + (cfg_.mode.empty() ? "(no mode)" : cfg_.mode) + " via " +
           (cfg_.tx_cmd.empty() ? "?" : fs::path(cfg_.tx_cmd.back()).filename().string());
}

std::string GreybeardModem::last_burst() const {
    std::lock_guard<std::mutex> g(mu_);
    return last_burst_;
}

bool GreybeardModem::send(const std::string& bytes) {
    if (!ready() || sending_) return false;
    sending_ = true;
    {
        std::lock_guard<std::mutex> g(mu_);
        tx_q_.push_back(bytes);
    }
    cv_.notify_all();
    return true;
}

void GreybeardModem::tx_loop() {
    while (running_) {
        std::string bytes;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(200), [this] { return !tx_q_.empty() || !running_; });
            if (tx_q_.empty()) continue;
            bytes = std::move(tx_q_.front());
            tx_q_.pop_front();
        }
        char stamp[32];
        std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", std::gmtime(&t));
        std::string base = std::string(stamp) + "-" + std::to_string(++seq_);
        fs::path payload = fs::path(cfg_.work_dir) / (base + ".bin");
        fs::path burst = fs::path(cfg_.outbox_dir) / (base + ".wav");
        fs::path part = fs::path(cfg_.work_dir) / (base + ".wav");  // moved to the outbox when whole

        bool ok = false;
        std::string why;
        {
            std::ofstream f(payload, std::ios::binary | std::ios::trunc);
            f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!f) why = "cannot write payload file";
        }
        if (why.empty()) {
            std::vector<std::string> argv = cfg_.tx_cmd;
            for (const std::string& a : {std::string("--waveform"), cfg_.waveform, std::string("--mode"), cfg_.mode,
                                         std::string("--in"), payload.string(), std::string("--out"), part.string()})
                argv.push_back(a);
            RunResult r = run_program(argv, cfg_.tx_timeout_ms);
            std::error_code ec;
            if (!r.started) why = "cannot start " + cfg_.tx_cmd.back() + ": " + r.err;
            else if (r.timed_out) why = "gale-tx timed out";
            else if (r.exit_code != 0) {
                why = "gale-tx exited " + std::to_string(r.exit_code);
                std::string e = r.err.substr(0, r.err.find('\n'));
                if (!e.empty()) why += ": " + e.substr(0, 160);
            } else if (fs::file_size(part, ec) <= 44 || ec) {
                why = "gale-tx wrote no audio";
            } else {
                // The WAV appears in the outbox only when whole.
                fs::rename(part, burst, ec);
                if (ec) {  // another file system: copy beside, then rename
                    ec.clear();
                    fs::path tmp = fs::path(cfg_.outbox_dir) / ("." + base + ".wav.tmp");
                    fs::copy_file(part, tmp, fs::copy_options::overwrite_existing, ec);
                    if (!ec) fs::rename(tmp, burst, ec);
                }
                if (ec) why = "cannot move burst into the outbox";
                else {
                    ok = true;
                    std::string summary = r.out.substr(0, r.out.find('\n'));
                    JsonValue j;
                    event("burst " + burst.filename().string() + (parse_json(summary, j) ? " " + summary.substr(0, 200)
                                                                                          : ""));
                    std::lock_guard<std::mutex> g(mu_);
                    last_burst_ = burst.string();
                }
            }
            if (!ok) fs::remove(part, ec);
        }
        std::error_code ec;
        fs::remove(payload, ec);
        sending_ = false;
        if (cb_.on_tx_done) cb_.on_tx_done(ok, why);
    }
}

void GreybeardModem::rx_loop() {
    while (running_) {
        scan_captures();
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(cfg_.poll_ms), [this] { return !running_.load(); });
    }
}

void GreybeardModem::scan_captures() {
    std::vector<std::string> todo;
    std::error_code ec;
    for (auto& de : fs::directory_iterator(cfg_.captures_dir, ec)) {
        std::string n = de.path().filename().string();
        if (!ends_with(n, kListing)) continue;
        std::lock_guard<std::mutex> g(mu_);
        if (!seen_.count(n)) todo.push_back(n);
    }
    std::sort(todo.begin(), todo.end());  // capture names start with their time
    for (auto& n : todo)
        if (handle_listing(n)) mark_seen(n);
}

void GreybeardModem::mark_seen(const std::string& name) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (!seen_.insert(name).second) return;
        first_seen_ms_.erase(name);
    }
    if (!cfg_.state_file.empty()) {
        std::ofstream f(cfg_.state_file, std::ios::app);
        f << name << "\n";
    }
}

bool GreybeardModem::handle_listing(const std::string& name) {
    fs::path dir(cfg_.captures_dir);
    long long t = now_ms();
    long long first;
    {
        std::lock_guard<std::mutex> g(mu_);
        first = first_seen_ms_.emplace(name, t).first->second;
    }
    bool give_up = t - first >= cfg_.settle_ms;

    std::string text;
    JsonValue v;
    if (!read_file(dir / name, text) || !parse_json(text, v)) {
        if (give_up) event("skipped " + name + ": not readable JSON");
        return give_up;  // may still be being written
    }
    auto entries = entries_of(v);
    std::vector<ModemDecode> payloads;
    size_t listed = 0;
    for (const JsonValue* e : entries) {
        // modem_verdict.py: one verdict per burst found; only a decoded data
        // burst has a payload file. Voice payloads are vocoder bits, not frames.
        const JsonValue* kind = e->get("payload_kind");
        if (e->get("waveform") && !first_of(*e, {"payload_file", "file", "name", "path", "bin"})) continue;
        if (kind && kind->type == JsonValue::Type::String && kind->s.find("harris") != std::string::npos) continue;
        ++listed;
        const JsonValue* f = first_of(*e, {"payload_file", "file", "name", "path", "bin"});
        const JsonValue* n = first_of(*e, {"payload_bytes", "bytes", "size", "byte_count", "length"});
        const JsonValue* h = first_of(*e, {"sha256", "sha_256", "digest"});
        if (!f || f->type != JsonValue::Type::String || !plain_name(f->s)) {
            event("skipped an entry in " + name + ": no plain file name");
            continue;
        }
        std::string data;
        bool good = read_file(dir / f->s, data);
        if (good && n && n->type == JsonValue::Type::Number && static_cast<double>(data.size()) != n->n) good = false;
        if (good && h && h->type == JsonValue::Type::String && sha256_hex(data) != h->s) good = false;
        if (!good) {
            if (!give_up) return false;  // wait for the file to be finished
            event("skipped " + f->s + ": missing, wrong size or wrong digest");
            continue;
        }
        if (!h) event("note: " + f->s + " has no sha256 in " + name);
        ModemDecode d;
        d.source = f->s;
        const JsonValue* wf = first_of(*e, {"waveform", "modem"});
        if (wf && wf->type == JsonValue::Type::String) d.waveform = wf->s;
        else if (f->s.find(".m110d.") != std::string::npos) d.waveform = "110D";
        else if (f->s.find(".m110a.") != std::string::npos) d.waveform = "110A";
        else d.waveform = cfg_.waveform;
        for (auto& c : d.waveform) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        d.sha256 = sha256_hex(data);
        d.bytes = std::move(data);
        payloads.push_back(std::move(d));
    }
    for (auto& p : payloads) {
        if (p.bytes.empty()) continue;
        if (cb_.on_decode) cb_.on_decode(p);
        if (cb_.on_rx) cb_.on_rx(p.bytes);
    }
    if (listed == 0) event(name + " lists no payload files");
    return true;
}

}  // namespace pna
