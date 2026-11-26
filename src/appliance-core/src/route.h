// The one route function: the core's Network layer (the networking controller,
// MIL-STD-188-141B D.4.2; docs/api.md, "Services").
//
// Every operator send path asks it for its route: send, compose_send,
// send_acp127, send_acp126, send_chat, replies, qsl, retransmit, requeue and
// release, and again at the airtime point and before a burst is handed to the
// station controller. It is the only place a crossing between services is
// refused. It stays apart from the Transport and Application code that calls it.
//
// A message takes the service of its destination's book entry. An address the
// book does not hold takes the service the operator picked. A broadcast (ALL)
// is a Network-layer address (D.5.2.8) and needs its service named. A crossing
// is a message whose service differs from its destination's, its route's or its
// channel's; the refusal names both services, and the item is never rerouted.
//
// Until stage 5 the automatic modes (routing, machine answers, auto-QSL, the
// BBS's ACKs) keep the MARS-only refusals they had (Core::world_refusal).
#pragma once

#include <string>

namespace pna {

class Ops;
class Services;

struct RouteAsk {
    std::string service;        // the message's service: the operator's pick, or the source's ("" none)
    std::string to;             // the destination: a call, an RI, or ALL
    std::string channel;        // the send's channel label ("" none given)
    std::string route_service;  // the service of the routing rule it goes on ("" none)
};

struct Route {
    bool ok = false;
    std::string service;  // the message's service, once resolved
    std::string refusal;  // why not, naming both services for a crossing
};

// services: the tables (nullptr: none, so every service is refused).
// ops: the station book and the channel plan (nullptr: none to look in).
Route route(const Services* services, Ops* ops, const RouteAsk& ask);

// The service a request names: `service`, or `world` for one release. Both,
// differing: false with *why.
bool request_service(const std::string& service, const std::string& world, std::string& out, std::string* why);

}  // namespace pna
