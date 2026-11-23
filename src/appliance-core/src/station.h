// Station keys: each appliance's own keypair, made on the box, and what it
// is used for. docs/crypto.md has the design.
//
//   Identity   the station's two private keys: Ed25519 to sign, X25519 to
//              receive sealed key tapes. Made locally (OpenSSL), kept in one
//              file beside the database (mode 0600), never sent anywhere.
//              The operator may make an encrypted backup (scrypt and
//              AES-256-GCM under a passphrase), to rebuild the station.
//   Card       the two public keys and the station's call, signed by the
//              station itself. Text in capitals and digits, so it goes by USB
//              or over the air in an ordinary plaindress message. Its
//              fingerprint, 16 letters and figures in four groups, is short
//              enough to read aloud on a voice check.
//   Signature  a [SIG:CALL:...] line on a message, over everything from the
//              date-time group (or FM line) through the closing BT. A
//              receiver checks it against the cards it holds.
//   Sealed tape  a net key tape enciphered to one station's card (X25519,
//              HKDF-SHA256, AES-256-GCM) and signed by the keymaster
//              station, so it can go over the air. Only the station it is
//              sealed to can open it, and it opens only if the keymaster's
//              card is held.
//
// A later hardware token (a YubiKey or the like) holds the same two kinds of
// key: the card format stays, and only where the private key lives changes.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pna {
namespace station {

struct Identity {
    std::string station;
    int64_t made = 0;
    std::string sign_priv, sign_pub;  // Ed25519, 32 bytes each
    std::string seal_priv, seal_pub;  // X25519, 32 bytes each
    void wipe();
};

struct Card {
    std::string station;
    int64_t made = 0;
    std::string sign_pub, seal_pub;
};

// A fresh identity for this station's call.
bool make_identity(const std::string& station, int64_t now, Identity& out, std::string* error);

// The identity file: read, or written whole (mode 0600, written beside and
// renamed into place). load() is false with *error when the file is
// damaged; *exists says whether there was one.
bool load_identity(const std::string& path, Identity& out, bool* exists, std::string* error);
bool save_identity(const std::string& path, const Identity& id, std::string* error);

// Encrypted backup under a passphrase (12 characters or more): the identity
// and the cards this station holds, with how each was trusted, so that a box
// rebuilt from a stale backup knows its keymasters and members again
// (version 2). Net keys are never in a backup: a rallying station gets fresh
// ones from a keymaster. restore() reads version 1 (identity only) too.
struct HeldCard {
    std::string card;    // the card's text
    bool checked = false;
    bool keymaster = false;
    std::string source;
};
bool backup(const Identity& id, const std::vector<HeldCard>& held, int64_t saved, const std::string& passphrase,
            std::string& out, std::string* error);
bool restore(const std::string& text, const std::string& passphrase, Identity& out, std::vector<HeldCard>* held,
             int64_t* saved, std::string* error);
inline bool backup(const Identity& id, const std::string& passphrase, std::string& out, std::string* error) {
    return backup(id, {}, 0, passphrase, out, error);
}
inline bool restore(const std::string& text, const std::string& passphrase, Identity& out, std::string* error) {
    return restore(text, passphrase, out, nullptr, nullptr, error);
}

Card card_of(const Identity& id);
std::string format_card(const Identity& id);  // signed by the identity
// Finds a card block anywhere in the text (a file, or a received message)
// and checks its self-signature. False with *error otherwise.
bool parse_card(const std::string& text, Card& out, std::string* error);
// The card block found in text, as its own text ("" when there is none).
std::string card_text(const std::string& text);
// "K7QM 2XRA 9FWD HT3C": 80 bits of SHA-256 over the call and both keys.
std::string fingerprint(const Card& c);

// Signing. sign() adds the [SIG:...] line before NNNN (after any [DIGEST:]).
std::string sign(const std::string& message, const Identity& id);
struct SigCheck {
    std::string state;   // "none", "good", "bad" (the text or signature changed), "unknown" (no card for the signer)
    std::string signer;  // the call on the [SIG:] line
};
SigCheck check_signature(const std::string& message, const std::function<std::optional<Card>(const std::string&)>& card_for);

// Sealed key tapes.
bool seal_tape(const std::string& tape_text, const std::string& net, const std::string& tape_id, const Identity& from,
               const Card& to, std::string& out, std::string* error);
struct SealedHead {
    std::string to, from, net, tape;
};
// Whether text holds a sealed tape block, and its plain header.
bool sealed_head(const std::string& text, SealedHead& out);
// Opens a sealed tape sealed to `me`, signed by a station whose card
// card_for() returns. False with *error otherwise.
bool open_tape(const std::string& text, const Identity& me,
               const std::function<std::optional<Card>(const std::string&)>& card_for, std::string& tape_text,
               SealedHead& head, std::string* error);

// Member handouts (docs/crypto.md). A keymaster gives each member of a net
// one handout, on paper or a USB stick. It holds no secret in the clear: the
// keymasters' cards, the members' fingerprints, the net's key tape sealed to
// that member (when the keymaster holds the member's card), and the steps.
// An alternate keymaster's handout also carries every member's card, so it
// can issue tapes if the keymaster is lost. The issuing keymaster signs the
// whole handout. A stale handout still works on a BAD_DAY: its cards and
// fingerprints stay good long after its tape has expired.
struct Handout {
    std::string net, for_station, by, tape;
    int64_t issued = 0, valid_from = 0, valid_until = 0;
    std::string first_key, first_check;  // the tape's first key and its check value
    std::vector<std::pair<std::string, std::string>> keymasters;  // call, fingerprint; the issuer first
    std::vector<std::pair<std::string, std::string>> members;     // call, fingerprint
    std::vector<std::string> cards;  // card texts: every keymaster's, and every member's in an alternate's handout
    std::string sealed;              // the sealed tape block, or ""
};
std::string format_handout(const Handout& h, const Identity& issuer);
// Checks the layout, every card's self-signature and fingerprint, and the
// issuer's signature against the issuer's card inside. False with *error.
bool parse_handout(const std::string& text, Handout& out, std::string* error);

}  // namespace station
}  // namespace pna
