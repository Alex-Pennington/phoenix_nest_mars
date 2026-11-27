#include "route.h"

#include "frame.h"
#include "channels.h"
#include "ops.h"
#include "services.h"

#include <cctype>

namespace pna {

namespace {

std::string clean_call(const std::string& call) {
    std::string c;
    for (char ch : call)
        if (ch != '@' && !std::isspace(static_cast<unsigned char>(ch)))
            c += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return c;
}

Route refused(const std::string& why) {
    Route r;
    r.refusal = why;
    return r;
}

std::string an(const std::string& s) {
    const char c = s.empty() ? 'x' : static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an " + s : "a " + s;
}

}  // namespace

bool request_service(const std::string& service, const std::string& world, std::string& out, std::string* why) {
    if (!service.empty() && !world.empty() && service != world) {
        if (why) *why = "service " + service + " and world " + world + " differ: give service only";
        return false;
    }
    out = service.empty() ? world : service;
    return true;
}

Route route(const Services* services, Ops* ops, const RouteAsk& ask) {
    const std::string to = clean_call(ask.to);
    const bool all = is_broadcast(to);

    // The destination's service: its book entry (the address list).
    std::string dest;
    std::string dest_name;
    if (!all && ops && !to.empty())
        if (auto e = ops->book_find(to)) {
            dest = e->world;
            dest_name = e->name;
        }

    std::string svc = ask.service;
    if (svc.empty()) svc = dest;
    if (svc.empty()) {
        if (all) return refused("refused: a broadcast (ALL) needs its service named");
        return refused("no service: an address not in the book needs the service picked at compose");
    }
    if (!services || !services->valid(svc)) {
        Route r = refused("refused: " + no_table(svc));
        r.no_table = valid_service_name(svc);  // a name with no table yet; "(none)" is no name
        return r;
    }

    if (!dest.empty() && dest != svc)
        return refused("refused: crossing: " + an(svc) + " message to " + an(dest) + " station (" +
                       (dest_name.empty() ? to : dest_name) + ")");
    if (!ask.route_service.empty() && ask.route_service != svc)
        return refused("refused: crossing: " + an(svc) + " message on " + an(ask.route_service) + " route");
    if (!ask.channel.empty()) {
        auto c = ops ? ops->channels().get(ask.channel) : std::nullopt;
        if (!c) return refused("refused: no channel " + ask.channel + " in the channel plan");
        if (c->world != svc)
            return refused("refused: crossing: " + an(svc) + " message on " + an(c->world) + " channel (" + c->name +
                           ")");
    }
    Route r;
    r.ok = true;
    r.service = svc;
    return r;
}

}  // namespace pna
