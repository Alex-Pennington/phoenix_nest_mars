#include "guide.h"

#include "compose.h"
#include "forms.h"
#include "json.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace pna {
namespace guide {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}
std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}
bool write_file(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}
bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}
bool good_id(const std::string& id) {
    if (id.empty() || id.size() > 24) return false;
    for (char c : id)
        if (!(std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' ||
              c == '_'))
            return false;
    return true;
}
bool made_id(const std::string& id) { return id.rfind("FORM-", 0) == 0 || id.rfind("TYPE-", 0) == 0; }

// Words wrapped to the page width. Blank lines and lines that start with a
// space (a laid-out example) are kept as they are.
std::vector<std::string> wrap(const std::string& text, size_t width = kWidth) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ' ' || line.size() <= width) {
            out.push_back(line);
            continue;
        }
        std::string cur;
        for (auto& w : words(line)) {
            if (!cur.empty() && cur.size() + 1 + w.size() > width) {
                out.push_back(cur);
                cur.clear();
            }
            cur += (cur.empty() ? "" : " ") + w;
        }
        if (!cur.empty()) out.push_back(cur);
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

}  // namespace

// ---------------------------------------------------------------- pages

bool parse(const std::string& text, Page& out, std::string* error) {
    out = Page{};
    if (text.size() > kMaxPage) return fail(error, "the page is longer than 16 KB");
    std::istringstream in(text);
    std::string raw;
    int n = 0;
    std::string block;  // TEXT or EXAMPLE while inside one
    std::string body;
    auto where = [&](const std::string& why) { return fail(error, "line " + std::to_string(n) + ": " + why); };
    while (std::getline(in, raw)) {
        ++n;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.find('\t') != std::string::npos) return where("a tab: use spaces");
        if (!block.empty()) {
            if (raw == "END") {
                while (!body.empty() && body.back() == '\n') body.pop_back();
                (block == "TEXT" ? out.text : out.example) = body;
                block.clear();
                body.clear();
                continue;
            }
            if (raw.size() > kWidth && raw[0] == ' ') return where("a laid-out line longer than 78 characters");
            body += raw + "\n";
            continue;
        }
        std::string l = trim(raw);
        if (l.empty() || l[0] == ';') continue;
        size_t sp = l.find(' ');
        std::string word = l.substr(0, sp), rest = sp == std::string::npos ? "" : trim(l.substr(sp + 1));
        if (word == "PAGE") {
            if (!good_id(upper(rest))) return where("PAGE needs an id: capitals, digits and dashes, 24 at most");
            out.id = upper(rest);
        } else if (word == "TITLE") {
            if (rest.empty() || rest.size() > 60) return where("TITLE needs a title of 60 characters at most");
            out.title = rest;
        } else if (word == "KIND") {
            std::string k = rest;
            for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (k != "start" && k != "screen" && k != "topic") return where("KIND is start, screen or topic");
            out.kind = k;
        } else if (word == "SCREENS") {
            for (auto& s : words(upper(rest))) {
                if (!good_id(s)) return where("a screen id is capitals, digits and dashes");
                out.screens.push_back(s);
            }
        } else if (word == "HELP") {
            if (rest.size() > kWidth) return where("HELP is one line of 78 characters at most");
            out.help = rest;
        } else if (word == "KEY") {
            size_t k = rest.find(' ');
            if (rest.empty() || k == std::string::npos) return where("KEY needs the key and what it does");
            out.keys.push_back({rest.substr(0, k), trim(rest.substr(k + 1))});
        } else if (word == "SEE") {
            for (auto& s : words(upper(rest))) out.see.push_back(s);
        } else if (word == "BEGIN_TEXT" || word == "BEGIN_EXAMPLE") {
            block = word == "BEGIN_TEXT" ? "TEXT" : "EXAMPLE";
        } else {
            return where("unknown line \"" + word + "\"");
        }
    }
    if (!block.empty()) return fail(error, "BEGIN_" + block + " has no END");
    if (out.id.empty()) return fail(error, "the page has no PAGE line");
    if (made_id(out.id))
        return fail(error, out.id + " is made from its template: change the form or message type instead");
    if (out.title.empty()) return fail(error, "the page has no TITLE");
    if (out.kind.empty()) out.kind = out.screens.empty() ? "topic" : "screen";
    if (compose::looks_like_frequency(text))
        return fail(error, "the page looks like it holds a frequency: name the channel by its designator instead");
    out.written = text;
    return true;
}

std::vector<Library::Entry> Library::list() const {
    std::map<std::string, Entry> all;
    for (auto& [name, text] : builtin_texts()) {
        Entry e;
        if (!parse(text, e.p, nullptr)) continue;
        e.origin = "built in";
        e.builtin = true;
        all[e.p.id] = e;
    }
    std::error_code ec;
    if (!dir_.empty() && fs::is_directory(dir_, ec)) {
        for (auto& de : fs::directory_iterator(dir_, ec)) {
            if (de.path().extension() != ".page") continue;
            Page p;
            if (!parse(read_file(de.path()), p, nullptr) || p.id != de.path().stem().string()) continue;
            Entry e;
            e.p = p;
            e.builtin = all.count(p.id) > 0;
            e.changed = !e.builtin || all[p.id].p.written != p.written;
            e.origin = e.changed ? "edited here" : "built in";
            all[p.id] = e;
        }
    }
    std::vector<Entry> out;
    for (auto& [id, e] : all) out.push_back(e);
    return out;
}

std::optional<Library::Entry> Library::get(const std::string& id) const {
    for (auto& e : list())
        if (e.p.id == upper(id)) return e;
    return std::nullopt;
}

bool Library::put(const std::string& text, std::string* result, std::string* id, std::string* error) {
    std::string norm = text;
    if (!norm.empty() && norm.back() != '\n') norm += '\n';
    Page p;
    if (!parse(norm, p, error)) return false;
    if (dir_.empty()) return fail(error, "pages can't be changed here");
    if (id) *id = p.id;
    auto old = get(p.id);
    if (old && old->p.written == norm) {
        if (result) *result = "same";
        return true;
    }
    if (!write_file(fs::path(dir_) / (p.id + ".page"), norm)) return fail(error, "couldn't write the page");
    if (result) *result = old ? "replaced" : "added";
    return true;
}

bool Library::reset(const std::string& id) {
    std::error_code ec;
    return fs::remove(fs::path(dir_) / (upper(id) + ".page"), ec);
}

// ---------------------------------------------------------------- made pages

namespace {

Page form_page(const forms::Template& t) {
    Page p;
    p.id = "FORM-" + t.id;
    p.title = t.title;
    p.kind = "form";
    p.screens = {p.id};
    p.source = "form " + t.id;
    p.help = "Tab next field  Enter send  Esc back  ? guide";
    std::string text = t.summary;
    if (!t.fields.empty()) text += "\n\nWhat goes in each box:";
    for (auto& f : t.fields) {
        text += "\n\n" + f.label + (f.required ? " (needed)" : "") + ":";
        if (!f.help.empty()) text += "\n" + f.help;
        else if (!f.choices.empty()) {
            std::string c;
            for (auto& ch : f.choices) c += (c.empty() ? "" : ", ") + (ch.label.empty() ? ch.value : ch.label);
            text += "\nOne of: " + c + ".";
        }
        if (f.secret) text += "\nNever shown again once entered.";
    }
    p.text = text;
    for (auto& n : t.next) p.see.push_back("FORM-" + n);
    return p;
}

Page type_page(const std::map<std::string, std::string>& t, const std::vector<std::map<std::string, std::string>>& fields) {
    auto at = [&](const std::string& k) {
        auto it = t.find(k);
        return it == t.end() ? std::string() : it->second;
    };
    Page p;
    p.id = "TYPE-" + at("id");
    p.title = at("title");
    p.kind = "type";
    p.source = "message type " + at("id");
    p.screens = {};
    std::string text = at("summary");
    if (!at("procedures").empty()) text += "\n\nGoes as: " + at("procedures") + ".";
    if (!fields.empty()) text += "\n\nWhat goes in each box:";
    for (auto& f : fields) {
        auto fa = [&](const char* k) {
            auto it = f.find(k);
            return it == f.end() ? std::string() : it->second;
        };
        text += "\n\n" + fa("label") + (fa("required") == "Y" ? " (needed)" : "") + ":";
        if (!fa("hint").empty()) text += "\n" + fa("hint");
        if (!fa("example").empty() && !compose::looks_like_frequency(fa("example")))
            text += "\nFor example: " + fa("example");
    }
    p.text = text;
    p.see = {"MSG-COMPOSE"};
    return p;
}

std::optional<Page> find(const Context& ctx, const std::string& id_in) {
    std::string id = upper(id_in);
    if (id.rfind("FORM-", 0) == 0 && ctx.forms) {
        if (auto e = ctx.forms->get(id.substr(5))) return form_page(e->t);
        return std::nullopt;
    }
    if (id.rfind("TYPE-", 0) == 0 && ctx.types) {
        for (auto& t : ctx.types()) {
            auto it = t.find("id");
            if (it != t.end() && upper(it->second) == id.substr(5))
                return type_page(t, ctx.type_fields ? ctx.type_fields(it->second) : decltype(ctx.type_fields(""))());
        }
        return std::nullopt;
    }
    if (ctx.pages)
        if (auto e = ctx.pages->get(id)) return e->p;
    return std::nullopt;
}

// The page laid out for 80 columns: what gale-term scrolls.
std::vector<std::string> lines_of(const Page& p, bool training) {
    std::vector<std::string> L;
    if (training) L.push_back("TRAINING: practice traffic only. Nothing here goes on the air.");
    L.push_back(p.title);
    L.push_back(std::string(std::min(p.title.size(), kWidth), '='));
    L.push_back("");
    for (auto& l : wrap(p.text)) L.push_back(l);
    if (!p.keys.empty()) {
        L.push_back("");
        L.push_back("Keys:");
        size_t w = 0;
        for (auto& k : p.keys) w = std::max(w, k.key.size());
        for (auto& k : p.keys) {
            std::string head = "  " + k.key + std::string(w - k.key.size() + 2, ' ');
            auto body = wrap(k.what, kWidth - head.size());
            for (size_t i = 0; i < body.size(); ++i)
                L.push_back((i == 0 ? head : std::string(head.size(), ' ')) + body[i]);
        }
    }
    if (!p.example.empty()) {
        L.push_back("");
        L.push_back("Example:");
        for (auto& l : wrap(p.example)) L.push_back(l);
    }
    if (!p.see.empty()) {
        std::string s;
        for (auto& x : p.see) s += (s.empty() ? "" : ", ") + x;
        L.push_back("");
        for (auto& l : wrap("See also: " + s)) L.push_back(l);
    }
    return L;
}

void write_page(JsonWriter& w, const Page& p, const std::string& origin, bool training) {
    w.field("id", p.id).field("title", p.title).field("kind", p.kind).field("help", p.help);
    w.begin_array("screens");
    for (auto& s : p.screens) w.field("", s);
    w.end_array();
    w.begin_array("keys");
    for (auto& k : p.keys) w.begin_object().field("key", k.key).field("what", k.what).end_object();
    w.end_array();
    w.field("text", p.text).field("example", p.example);
    w.begin_array("see");
    for (auto& s : p.see) w.field("", s);
    w.end_array();
    w.field("origin", origin);
    if (!p.source.empty()) w.field("source", p.source);
    w.begin_array("lines");
    for (auto& l : lines_of(p, training)) w.field("", l);
    w.end_array();
}

std::string error_reply(const std::string& e) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", e).end_object();
    return w.str();
}

}  // namespace

std::vector<Step> setup_steps(bool call_set) {
    std::vector<Step> s = {
        {"SETUP", "Give the box your call", "SETUP", "FORM-SETUP",
         "Type your call once. The box makes its station keys and its card.", false},
        {"CARD", "Show your card to your keymaster", "CARD_SAVE", "FORM-CARD_SAVE",
         "Put your card on a stick (CARD_SAVE) or send it (CARD_SEND), and read your fingerprint to your "
         "keymaster by voice or in person.",
         false},
        {"KEYMASTER", "Choose whose authority you take", "KEYMASTER", "FORM-KEYMASTER",
         "Take their card (CARD_LOAD), check its fingerprint by voice (CARD_CHECK), then accept them "
         "(KEYMASTER). The choice is yours alone.",
         false},
        {"CHANNELS", "Enter your channels", "BUNDLE_LOAD", "FORM-BUNDLE_LOAD",
         "Load the startup bundle your keymaster gave you (BUNDLE_LOAD), or type each channel from your "
         "card (CHANNEL).",
         false},
        {"SCHEDULE", "Know when your nets meet", "SCHEDULE", "FORM-SCHEDULE",
         "A bundle brings the schedule with it. Without one, enter each net (SCHEDULE).", false},
        {"KEYS", "Load your key tape", "KEY_OPEN", "FORM-KEY_OPEN",
         "Open the tape your keymaster sealed to you (KEY_OPEN), or load a handout from a stick "
         "(HANDOUT_LOAD).",
         false},
        {"FIRST", "Send your first message", "", "MSG-COMPOSE",
         "Press c, pick a message type and fill it in. Practice first in training mode if you like.", false},
    };
    s[0].done = call_set;
    return s;
}

std::optional<std::string> handle(const Context& ctx, const std::string& cmd, std::map<std::string, std::string>& req) {
    if (cmd.rfind("guide_", 0) != 0) return std::nullopt;

    if (cmd == "guide_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).field("training", ctx.training).begin_array("pages");
        auto entry = [&](const Page& p, const std::string& origin) {
            w.begin_object().field("id", p.id).field("title", p.title).field("kind", p.kind).field("origin", origin);
            w.begin_array("screens");
            for (auto& s : p.screens) w.field("", s);
            w.end_array().end_object();
        };
        if (ctx.pages)
            for (auto& e : ctx.pages->list()) entry(e.p, e.origin);
        if (ctx.forms)
            for (auto& e : ctx.forms->list()) entry(form_page(e.t), e.origin);
        if (ctx.types)
            for (auto& t : ctx.types()) entry(type_page(t, {}), "built in");
        w.end_array().end_object();
        return w.str();
    }

    if (cmd == "guide_get" || cmd == "guide_help") {
        std::optional<Page> p;
        std::string asked = upper(cmd == "guide_get" ? req["id"] : req["screen"]);
        if (asked.empty()) return error_reply(cmd == "guide_get" ? "name the page (id)" : "name the screen (screen)");
        std::string origin = "built in";
        if (cmd == "guide_get") {
            p = find(ctx, asked);
        } else {
            // The page that answers ? on this screen (ask 39), in this order:
            // a form's own page; a page written or changed on this box whose
            // SCREENS line lists the screen; a built-in page that lists it;
            // the page whose id is the screen. Within each, pages go by id.
            if (asked.rfind("FORM-", 0) == 0) p = find(ctx, asked);
            if (!p && ctx.pages) {
                auto all = ctx.pages->list();
                for (bool local : {true, false}) {
                    for (auto& e : all)
                        if (e.changed == local &&
                            std::find(e.p.screens.begin(), e.p.screens.end(), asked) != e.p.screens.end()) {
                            p = e.p;
                            origin = e.origin;
                            break;
                        }
                    if (p) break;
                }
            }
            if (!p) p = find(ctx, asked);
        }
        if (p && ctx.pages && p->kind != "form" && p->kind != "type")
            if (auto e = ctx.pages->get(p->id)) origin = e->origin;
        if (p && p->kind == "form" && ctx.forms)
            if (auto e = ctx.forms->get(p->id.substr(5))) origin = e->origin;
        JsonWriter w;
        if (!p) {
            // No page yet: the main screen's, so ? never leads nowhere.
            auto main = find(ctx, "MAIN");
            w.begin_object().field("ok", true).field("found", false).field("asked", asked).field("training", ctx.training);
            if (main) {
                w.begin_object("page");
                write_page(w, *main, "built in", ctx.training);
                w.end_object();
            }
            return w.end_object().str();
        }
        w.begin_object().field("ok", true).field("found", true).field("asked", asked).field("training", ctx.training);
        w.begin_object("page");
        write_page(w, *p, origin, ctx.training);
        w.end_object();
        // On the compose screen, the page for the message type being written.
        if (cmd == "guide_help" && !req["type"].empty())
            if (auto t = find(ctx, "TYPE-" + upper(req["type"]))) {
                w.begin_object("type");
                write_page(w, *t, "built in", ctx.training);
                w.end_object();
            }
        return w.end_object().str();
    }

    if (cmd == "guide_start") {
        auto steps = ctx.steps ? ctx.steps() : setup_steps(false);
        int next = -1, done = 0;
        for (size_t i = 0; i < steps.size(); ++i) {
            if (steps[i].done) ++done;
            else if (next < 0) next = static_cast<int>(i);
        }
        JsonWriter w;
        w.begin_object().field("ok", true).field("training", ctx.training).field("done", done).field("of",
                                                                                                     static_cast<int>(steps.size()));
        w.begin_array("steps");
        for (size_t i = 0; i < steps.size(); ++i)
            w.begin_object()
                .field("n", static_cast<int>(i + 1))
                .field("id", steps[i].id)
                .field("title", steps[i].title)
                .field("form", steps[i].form)
                .field("screen", steps[i].screen)
                .field("how", steps[i].how)
                .field("done", steps[i].done)
                .end_object();
        w.end_array();
        if (next >= 0) w.field("next", steps[next].id).field("next_title", steps[next].title).field("next_screen", steps[next].screen);
        else w.field("next", std::string()).field("next_title", std::string("Every step is done"));
        if (auto p = find(ctx, "START")) {
            w.begin_object("page");
            write_page(w, *p, "built in", ctx.training);
            w.end_object();
        }
        return w.end_object().str();
    }

    if (cmd == "guide_page_get") {
        std::string id = upper(req["id"]);
        if (made_id(id)) return error_reply(id + " is made from its template: open the form or message type instead");
        if (!ctx.pages) return error_reply("no pages here");
        auto e = ctx.pages->get(id);
        if (!e) return error_reply("no page " + id);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", e->p.id).field("origin", e->origin).field("text", e->p.written);
        return w.end_object().str();
    }
    if (cmd == "guide_page_put") {
        if (!ctx.pages) return error_reply("pages can't be changed here");
        std::string result, id, err;
        if (!ctx.pages->put(req["text"], &result, &id, &err)) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", id).field("result", result);
        return w.end_object().str();
    }
    if (cmd == "guide_page_reset") {
        if (!ctx.pages) return error_reply("pages can't be changed here");
        bool had = ctx.pages->reset(req["id"]);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", upper(req["id"])).field("reset", had);
        return w.end_object().str();
    }
    return error_reply("unknown guide call " + cmd);
}

}  // namespace guide
}  // namespace pna
