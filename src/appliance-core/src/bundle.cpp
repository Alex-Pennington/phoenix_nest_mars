// Startup bundles. See bundle.h and docs/crypto.md.
#include "bundle.h"
#include "forms.h"
#include "parts.h"

#include "compose.h"
#include "crypto.h"
#include "keys.h"
#include "sha256.h"

#include <algorithm>
#include <cstring>

namespace pna {
namespace bundle {

namespace {

const char* kHead = "PNA STARTUP BUNDLE 1";
const char* kEnd = "END BUNDLE";
const char* kFreqHead = "PNA SEALED PLAN 1";
constexpr size_t kLine = 69;  // ACP 127's line

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string simplified(const std::string& s) {
    std::string o;
    bool gap = false;
    for (char c : s) {
        if (space(c)) {
            gap = !o.empty();
        } else {
            if (gap) o += ' ';
            gap = false;
            o += c;
        }
    }
    return o;
}

std::vector<std::string> lines_of(const std::string& t) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : t) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool starts(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

// "KEY rest" -> rest, when the line's first word is key.
bool field(const std::string& l, const char* key, std::string& rest) {
    size_t n = std::strlen(key);
    if (l.compare(0, n, key) != 0) return false;
    if (l.size() == n) {
        rest.clear();
        return true;
    }
    if (l[n] != ' ') return false;
    rest = l.substr(n + 1);
    return true;
}

// Free text cut into lines of KEY + up to kLine, at spaces.
void text_lines(std::vector<std::string>& out, const std::string& key, const std::string& text) {
    size_t room = kLine - key.size() - 1;
    std::string cur, word;
    auto flush = [&] {
        if (!cur.empty()) out.push_back(key + " " + cur);
        cur.clear();
    };
    std::string t = text + " ";
    for (char c : t) {
        if (c != ' ') {
            word += c;
            continue;
        }
        if (word.empty()) continue;
        if (word.size() > room) word.resize(room);
        if (!cur.empty() && cur.size() + 1 + word.size() > room) flush();
        cur += (cur.empty() ? "" : " ") + word;
        word.clear();
    }
    flush();
}

std::string tag_of(const std::vector<std::string>& lines) {
    std::string t;
    for (auto& l : lines) t += l + "\n";
    return upper(sha256_hex(t).substr(0, 8));
}

std::string signed_text(const std::vector<std::string>& body) {
    std::string t = "PNA BUNDLE 1|";
    for (auto& l : body) t += l + "\n";
    return t;
}

// '@' is an ALE wildcard but not a radio character: it travels as '/'.
std::string ale_out(std::string s) {
    std::replace(s.begin(), s.end(), '@', '/');
    return s;
}
std::string ale_in(std::string s) {
    std::replace(s.begin(), s.end(), '/', '@');
    return s;
}

std::string yn(bool b) { return b ? "Y" : "N"; }

std::string hhmm(int minutes) {
    char b[24];
    std::snprintf(b, sizeof b, "%02d%02d", minutes / 60 % 100, minutes % 60);
    return b;
}

bool same_entry(const BookEntry& a, const BookEntry& b) {
    return a.name == b.name && a.kind == b.kind && a.call == b.call && a.ri == b.ri && a.ale == b.ale &&
           a.members == b.members && a.note == b.note;
}

}  // namespace

std::string radio_text(const std::string& s) {
    std::string o;
    for (char c : upper(s)) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '?' ||
                  c == ':' || c == '(' || c == ')' || c == '.' || c == ',' || c == '/';
        o += ok ? c : ' ';
    }
    return simplified(o);
}

Bundle collect(Ops& ops, const std::vector<std::pair<std::string, std::string>>& links, const std::string& label,
               std::vector<std::string>* changed) {
    auto note = [&](const std::string& s) {
        if (changed) changed->push_back(s);
    };
    // A name must come through the radio unchanged, or the entry is left out.
    auto name_ok = [&](const std::string& what, const std::string& n) {
        if (radio_text(n) == simplified(upper(n)) && n.size() + 9 <= kLine) return true;
        note(what + " " + n + " left out: its name has a character the radio can't send, or is too long");
        return false;
    };
    // Free text is made radio-safe; a frequency never goes.
    auto prose = [&](const std::string& what, const std::string& s) {
        std::string r = radio_text(s);
        if (compose::looks_like_frequency(r)) {
            note(what + ": text left out because it looks like a frequency");
            return std::string();
        }
        if (r != simplified(upper(s))) note(what + ": text changed to the radio's characters");
        return r;
    };
    Bundle b;
    b.label = radio_text(label);
    for (auto& e : ops.book_list()) {
        if (!name_ok("book entry", e.name)) continue;
        BookEntry o = e;
        o.ale = ale_out(e.ale);
        bool bad = false;
        for (auto* f : {&o.call, &o.ri, &o.ale})
            if (radio_text(*f) != *f) bad = true;
        for (auto& m : o.members)
            if (radio_text(m) != m) bad = true;
        if (bad) {
            note("book entry " + e.name + " left out: a call, RI, ALE address or member has a character the radio can't send");
            continue;
        }
        o.note = prose("book entry " + e.name + " note", e.note);
        b.book.push_back(o);
    }
    for (auto& c : ops.channels().list()) {
        // A bundle is MARS traffic: an amateur channel never goes in it (asks 37, 40).
        if (c.world == "amateur") continue;
        if (!name_ok("channel", c.name)) continue;
        Channel o;
        o.name = c.name;
        o.mode = radio_text(c.mode);
        o.use = prose("channel " + c.name + " use", c.use);
        b.channels.push_back(o);  // hz stays 0: in the clear, a channel goes by designator only
        if (c.hz > 0) b.freqs.emplace_back(c.name, c.hz);  // sealed per member, never in the clear
    }
    for (auto& s : ops.sched_list()) {
        if (!name_ok("schedule", s.name)) continue;
        NetSchedule o = s;
        o.note = prose("schedule " + s.name + " note", s.note);
        o.last_start = 0;
        o.last_net.clear();
        bool bad = radio_text(o.ncs) != o.ncs || radio_text(o.channel) != o.channel;
        for (auto& r : o.roster) bad = bad || radio_text(r) != r || r.size() + 7 > kLine;
        if (bad) {
            note("schedule " + s.name + " left out: its net control, channel or roster has a character the radio can't send");
            continue;
        }
        b.schedules.push_back(o);
    }
    for (auto& l : links) {
        std::string st = radio_text(l.first);
        if (st != l.first || (l.second != "plain" && l.second != "pna1")) continue;
        b.links.push_back(l);
    }
    return b;
}

std::string format(const Bundle& b, const station::Identity& signer, const station::Card* to) {
    station::Card card = station::card_of(signer);
    std::string for_station = to ? to->station : b.for_station;
    std::string sealed;
    if (to && !b.freqs.empty()) {
        std::string plain;
        for (auto& f : b.freqs) plain += f.first + " " + std::to_string(f.second) + "\n";
        std::string err;
        if (!station::seal_block(kFreqHead, {}, plain, signer, *to, sealed, &err)) sealed.clear();
        crypto::wipe(plain);
    }
    std::vector<std::string> L = {kHead};
    if (!b.label.empty()) L.push_back("LABEL " + b.label.substr(0, kLine - 6));
    L.push_back("BY " + signer.station);
    L.push_back("MADE " + keys::time_text(b.made));
    L.push_back("SIGNER " + signer.station + " " + station::fingerprint(card));
    if (!for_station.empty()) L.push_back("FOR " + for_station);
    std::vector<std::string> steps = {"STEP 1 CHECK THE SIGNER FINGERPRINT ABOVE BY VOICE OR IN PERSON.",
                                      "STEP 2 LOAD THIS BUNDLE ON YOUR APPLIANCE (BUNDLE LOAD).",
                                      "STEP 3 LOADING IT CHANGES NO CARD, KEY OR KEYMASTER."};
    if (!sealed.empty()) {
        steps.push_back("STEP 4 THE FREQUENCIES ARE SEALED BELOW TO " + for_station + ".");
        steps.push_back("STEP 5 THEY OPEN ONLY ON THE BOX OF " + for_station + ".");
    } else if (!for_station.empty() && !to) {
        steps.push_back("STEP 4 NO FREQUENCIES: SEND YOUR CARD TO " + signer.station + " (CARD SEND) AND");
        steps.push_back("STEP 5 CHECK IT BY VOICE, FOR A BUNDLE WITH THEM SEALED TO YOU.");
    } else {
        steps.push_back("STEP 4 CHANNELS ARE NAMED BY DESIGNATOR ONLY. LOAD YOUR");
        steps.push_back("STEP 5 FREQUENCY CARD FOR ANY CHANNEL YOUR BOX DOES NOT HOLD.");
    }
    for (auto& s : steps) L.push_back(radio_text(s).substr(0, kLine));
    for (auto& e : b.book) {
        L.push_back("BOOK " + e.name);
        L.push_back(std::string("KIND ") + (e.kind == "collective" ? "COLLECTIVE" : "STATION"));
        if (!e.call.empty() && e.call != e.name) L.push_back("CALL " + e.call);
        if (!e.ri.empty()) L.push_back("RI " + e.ri);
        if (!e.ale.empty()) L.push_back("ALE " + e.ale);
        for (auto& m : e.members) L.push_back("MEMBER " + m);
        if (!e.note.empty()) text_lines(L, "NOTE", e.note);
    }
    for (auto& c : b.channels) {
        L.push_back("CHANNEL " + c.name);
        if (!c.mode.empty()) L.push_back("MODE " + c.mode.substr(0, kLine - 5));
        if (!c.use.empty()) text_lines(L, "USE", c.use);
    }
    for (auto& s : b.schedules) {
        L.push_back("SCHEDULE " + s.name);
        L.push_back("DAYS " + s.days);
        L.push_back("START " + hhmm(s.start));
        L.push_back("MINUTES " + std::to_string(s.minutes));
        if (!s.channel.empty()) L.push_back("CHAN " + s.channel);
        if (!s.ncs.empty()) L.push_back("NCS " + s.ncs);
        L.push_back("EXERCISE " + yn(s.exercise));
        L.push_back("AUTO " + yn(s.auto_open));
        for (auto& r : s.roster) L.push_back("ROSTER " + r);
        if (!s.note.empty()) text_lines(L, "NOTE", s.note);
    }
    for (auto& l : b.links) L.push_back("LINK " + l.first + " " + upper(l.second));
    for (auto& [id, text] : b.forms) {
        // _ can't go in plaindress: the id travels with - (form ids hold no -).
        std::string sent = id;
        std::replace(sent.begin(), sent.end(), '_', '-');
        L.push_back("FORM " + sent);
        for (auto& d : forms::pack(text)) L.push_back("FORMDATA " + d);
    }
    for (auto& l : lines_of(sealed))
        if (!simplified(l).empty()) L.push_back(simplified(l));
    for (auto& l : lines_of(station::format_card(signer)))
        if (!simplified(l).empty()) L.push_back(simplified(l));
    std::string sig = upper(crypto::to_hex(crypto::ed25519_sign(signer.sign_priv, signed_text(L))));
    std::string out;
    for (auto& l : L) out += l + "\n";
    out += "SIG " + sig.substr(0, 64) + "\nSIG " + sig.substr(64) + "\n" + kEnd + "\n";
    return out;
}

bool parse(const std::string& text, Bundle& out, std::string* error) {
    out = Bundle{};
    std::vector<std::string> L;
    bool in = false, ended = false;
    for (auto& raw : lines_of(upper(text))) {
        std::string l = simplified(raw);
        if (!in) {
            if (l == kHead) {
                in = true;
                L.push_back(l);
            }
            continue;
        }
        if (l.empty()) continue;
        if (l == kEnd) {
            ended = true;
            break;
        }
        L.push_back(l);
    }
    if (!in) return fail(error, std::string("no startup bundle in the text (it starts ") + kHead + ")");
    if (!ended || L.size() < 6 || !starts(L[L.size() - 1], "SIG ") || !starts(L[L.size() - 2], "SIG "))
        return fail(error, "the bundle is cut off or damaged");
    std::string sig;
    if (!crypto::from_hex(L[L.size() - 2].substr(4) + L[L.size() - 1].substr(4), sig) || sig.size() != 64)
        return fail(error, "the bundle's signature is damaged");
    std::vector<std::string> body(L.begin(), L.end() - 2);
    std::vector<std::string> all = L;
    all.push_back(kEnd);
    out.tag = tag_of(all);

    // The signer's card: the block at the end of the body.
    size_t card_at = body.size();
    for (size_t i = 0; i < body.size(); ++i)
        if (body[i] == "PNA STATION CARD 1") card_at = i;
    if (card_at == body.size()) return fail(error, "the bundle carries no signer's card");
    std::string ct;
    for (size_t i = card_at; i < body.size(); ++i) ct += body[i] + "\n";
    station::Card card;
    if (!station::parse_card(ct, card, error)) return false;
    out.signer_card = station::card_text(ct);
    out.signer_fp = station::fingerprint(card);

    enum { NONE, BOOK, CHAN, SCHED, FORMREC } cur = NONE;
    std::vector<std::vector<std::string>> form_data;
    std::string v;
    for (size_t i = 1; i < card_at; ++i) {
        const std::string& l = body[i];
        if (l == kFreqHead) {
            if (!out.sealed_freqs.empty()) return fail(error, "the bundle carries two sealed frequency blocks");
            size_t j = i;
            for (; j < card_at; ++j) {
                out.sealed_freqs += body[j] + "\n";
                if (body[j] == "END") break;
            }
            if (j == card_at) return fail(error, "the bundle's sealed frequencies are cut off");
            i = j;
            cur = NONE;
            continue;
        }
        if (field(l, "BOOK", v)) {
            BookEntry e;
            e.name = v;
            out.book.push_back(e);
            cur = BOOK;
        } else if (field(l, "CHANNEL", v)) {
            Channel c;
            c.name = v;
            out.channels.push_back(c);
            cur = CHAN;
        } else if (field(l, "SCHEDULE", v)) {
            NetSchedule s;
            s.name = v;
            s.minutes = 60;
            out.schedules.push_back(s);
            cur = SCHED;
        } else if (field(l, "FORM", v)) {
            std::replace(v.begin(), v.end(), '-', '_');
            out.forms.push_back({v, ""});
            form_data.emplace_back();
            cur = FORMREC;
        } else if (cur == FORMREC && field(l, "FORMDATA", v)) {
            form_data.back().push_back(v);
        } else if (field(l, "LINK", v)) {
            size_t sp = v.find(' ');
            if (sp != std::string::npos) {
                std::string f = v.substr(sp + 1);
                for (auto& c : f) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                out.links.emplace_back(v.substr(0, sp), f);
            }
            cur = NONE;
        } else if (cur == NONE) {
            if (field(l, "LABEL", v)) out.label = v;
            else if (field(l, "BY", v)) out.by = v;
            else if (field(l, "MADE", v)) keys::parse_time(v, out.made);
            else if (field(l, "FOR", v)) out.for_station = v;
            else if (field(l, "SIGNER", v)) {
                if (v.compare(0, out.by.size() + 1, out.by + " ") != 0 || v.substr(out.by.size() + 1) != out.signer_fp)
                    return fail(error, "the bundle's SIGNER line doesn't match the card inside it");
            }
        } else if (cur == BOOK) {
            BookEntry& e = out.book.back();
            if (field(l, "KIND", v)) e.kind = v == "COLLECTIVE" ? "collective" : "station";
            else if (field(l, "CALL", v)) e.call = v;
            else if (field(l, "RI", v)) e.ri = v;
            else if (field(l, "ALE", v)) e.ale = ale_in(v);
            else if (field(l, "MEMBER", v)) e.members.push_back(v);
            else if (field(l, "NOTE", v)) e.note += (e.note.empty() ? "" : " ") + v;
        } else if (cur == CHAN) {
            Channel& c = out.channels.back();
            if (field(l, "MODE", v)) c.mode = v;
            else if (field(l, "USE", v)) c.use += (c.use.empty() ? "" : " ") + v;
        } else if (cur == SCHED) {
            NetSchedule& s = out.schedules.back();
            if (field(l, "DAYS", v)) s.days = v;
            else if (field(l, "START", v)) parse_hhmm(v, &s.start);
            else if (field(l, "MINUTES", v)) s.minutes = std::atoi(v.c_str());
            else if (field(l, "CHAN", v)) s.channel = v;
            else if (field(l, "NCS", v)) s.ncs = v;
            else if (field(l, "EXERCISE", v)) s.exercise = v == "Y";
            else if (field(l, "AUTO", v)) s.auto_open = v == "Y";
            else if (field(l, "ROSTER", v)) s.roster.push_back(v);
            else if (field(l, "NOTE", v)) s.note += (s.note.empty() ? "" : " ") + v;
        }
    }
    for (size_t i = 0; i < out.forms.size(); ++i)
        if (!forms::unpack(form_data[i], out.forms[i].second))
            return fail(error, "form " + out.forms[i].first + " in the bundle is damaged");
    for (auto& e : out.book)
        if (e.kind == "station" && e.call.empty()) e.call = e.name;
    if (out.by.empty() || out.made == 0 || card.station != out.by)
        return fail(error, "the bundle's heading is damaged, or its card isn't the signer's");
    bool signer_line = false;
    for (size_t i = 1; i < card_at; ++i) signer_line = signer_line || starts(body[i], "SIGNER ");
    if (!signer_line) return fail(error, "the bundle has no SIGNER line");
    if (!crypto::ed25519_verify(card.sign_pub, signed_text(body), sig))
        return fail(error, "the bundle's signature does not check against " + out.by +
                               "'s card in it: it was changed or damaged");
    if (!out.sealed_freqs.empty()) {
        std::string to, from, w;
        for (auto& l : lines_of(out.sealed_freqs)) {
            if (field(l, "TO", w) && to.empty()) to = w;
            else if (field(l, "FROM", w) && from.empty()) from = w;
        }
        if (to != out.for_station || from != out.by)
            return fail(error, "the bundle's sealed frequencies aren't for its FOR station, or not from its signer");
    }
    return true;
}

bool open_freqs(const Bundle& b, const station::Identity& me, std::vector<std::pair<std::string, int64_t>>& out,
                std::string* error) {
    out.clear();
    if (b.sealed_freqs.empty()) return fail(error, "the bundle carries no frequencies");
    station::Card signer;
    if (!station::parse_card(b.signer_card, signer, error)) return false;
    std::string plain, from;
    if (!station::open_block(kFreqHead, b.sealed_freqs, me,
                             [&](const std::string& c) -> std::optional<station::Card> {
                                 if (c == signer.station) return signer;
                                 return std::nullopt;
                             },
                             plain, from, "sealed frequency block", error))
        return false;
    for (auto& l : lines_of(plain)) {
        std::string t = simplified(l);
        size_t sp = t.find(' ');
        if (t.empty() || sp == std::string::npos) continue;
        int64_t hz = std::atoll(t.c_str() + sp + 1);
        if (hz > 0) out.emplace_back(t.substr(0, sp), hz);
    }
    crypto::wipe(plain);
    return true;
}

FreqApplied apply_freqs(const Bundle& b, const std::vector<std::pair<std::string, int64_t>>& freqs, ChannelPlan& plan,
                        bool may_change, const std::string& source) {
    FreqApplied a;
    for (auto& f : freqs) {
        auto have = plan.get(f.first);
        Channel n;
        if (have) {
            if (have->hz == f.second) {
                ++a.same;
                continue;
            }
            // A MARS bundle never changes a channel this station keeps for the amateur world.
            if (!may_change || have->world == "amateur") {
                ++a.kept;
                continue;
            }
            n = *have;
        } else {
            n.name = f.first;
            n.world = "mars";  // a MARS bundle's channel (see the book entries above)
            for (auto& c : b.channels)
                if (c.name == f.first) {
                    n.mode = c.mode;
                    n.use = c.use;
                }
        }
        n.hz = f.second;
        n.source = source;
        std::string err;
        if (plan.put(n, &err)) ++(have ? a.changed : a.added);
        else a.problems.push_back(err);
    }
    return a;
}

std::vector<std::string> split(const std::string& bundle_text, size_t max_body) {
    std::vector<std::string> L;
    bool in = false;
    for (auto& raw : lines_of(upper(bundle_text))) {
        std::string l = simplified(raw);
        if (!in && l != kHead) continue;
        in = true;
        if (l.empty()) continue;
        L.push_back(l);
        if (l == kEnd) break;
    }
    std::string text;
    for (auto& l : L) text += l + "\n";
    return parts::split("BUNDLE", tag_of(L), text, max_body);
}

bool part_head(const std::string& text, std::string& tag, int& n, int& of) {
    parts::Head h;
    if (!parts::head(text, h) || h.kind != "BUNDLE") return false;
    tag = h.tag;
    n = h.n;
    of = h.of;
    return true;
}

std::string part_body(const std::string& text) { return parts::body(text); }

Applied apply(const Bundle& b, Ops& ops, const std::string& source) {
    Applied a;
    for (auto e : b.book) {
        auto old = ops.book_get(e.name);
        e.source = source;
        // A startup bundle is the MARS net's (bundle.h): its entries are of
        // that service until stage 2 takes it from the bundle (greybeard-ale #18).
        if (e.world.empty()) e.world = old ? old->world : "mars";
        std::string err;
        if (old && same_entry(*old, e)) {
            ++a.book_same;
            continue;
        }
        if (!ops.book_put(e, &err)) {
            a.problems.push_back("book entry " + e.name + ": " + err);
            continue;
        }
        ++(old ? a.book_replaced : a.book_added);
    }
    for (auto& c : b.channels) {
        auto have = ops.channels().get(c.name);
        if (!have) {
            a.need_frequency.push_back(c.name);
            continue;
        }
        if (have->world == "amateur") continue;  // kept for the amateur world: a MARS bundle leaves it
        Channel n = *have;
        if (!c.mode.empty()) n.mode = c.mode;
        if (!c.use.empty()) n.use = c.use;
        if (n.mode == have->mode && n.use == have->use) continue;
        std::string err;
        if (ops.channels().put(n, &err)) ++a.channels_updated;
        else a.problems.push_back("channel " + c.name + ": " + err);
    }
    for (auto s : b.schedules) {
        auto have = ops.sched_get(s.name);
        if (have) {
            if (have->days == s.days && have->start == s.start && have->minutes == s.minutes &&
                have->roster == s.roster && have->exercise == s.exercise && have->ncs == s.ncs &&
                have->note == s.note && have->auto_open == s.auto_open && have->channel == s.channel) {
                ++a.schedules_same;
                continue;
            }
            // The sitting this box last opened stays, so a reload doesn't open it twice.
            s.last_start = have->last_start;
            s.last_net = have->last_net;
        }
        std::string err;
        if (!ops.sched_put(s, &err)) {
            a.problems.push_back("schedule " + s.name + ": " + err);
            continue;
        }
        ++(have ? a.schedules_replaced : a.schedules_added);
    }
    return a;
}

}  // namespace bundle
}  // namespace pna
