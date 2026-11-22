#include "../src/frame.h"
#include "../src/json.h"
#include "check.h"

#include <map>

using namespace pna;

static Frame msg(const std::string& id, const std::string& body, char prec = 'R') {
    Frame f;
    f.id = id;
    f.src = "A";
    f.dst = "B";
    f.prec = prec;
    f.body = body;
    return f;
}

int main() {
    // CRC-32 check value from the standard.
    CHECK(crc32("123456789", 9) == 0xCBF43926u);

    // Round trip, fed one byte at a time.
    {
        Frame f = msg("A-1", "HELLO FROM A\r\nSECOND LINE", 'O');
        std::string wire = encode(f);
        FrameParser p;
        std::vector<Frame> got;
        for (char c : wire)
            for (auto& g : p.feed(std::string(1, c))) got.push_back(g);
        CHECK(got.size() == 1);
        if (got.size() == 1) {
            CHECK(got[0].id == "A-1");
            CHECK(got[0].src == "A");
            CHECK(got[0].dst == "B");
            CHECK(got[0].prec == 'O');
            CHECK(got[0].body == f.body);
            CHECK(got[0].type == FrameType::Msg);
        }
    }

    // Body may hold SOH and EOT bytes.
    {
        Frame f = msg("A-2", std::string("x\x01PNA1 y\x04z", 11));
        FrameParser p;
        auto got = p.feed(encode(f));
        CHECK(got.size() == 1 && got[0].body == f.body);
    }

    // Noise before, between and after frames; trailing NULs as the modem pads.
    {
        Frame a = msg("A-3", "ONE"), b = msg("A-4", "TWO");
        Frame ack;
        ack.type = FrameType::Ack;
        ack.id = "A-3";
        ack.src = "B";
        ack.dst = "A";
        std::string wire = "\xff\x13garbage" + encode(a) + "noise\x01junk\n" + encode(b) + encode(ack) +
                           std::string(5, '\0');
        FrameParser p;
        auto got = p.feed(wire);
        CHECK(got.size() == 3);
        if (got.size() == 3) {
            CHECK(got[0].body == "ONE");
            CHECK(got[1].body == "TWO");
            CHECK(got[2].type == FrameType::Ack && got[2].id == "A-3");
        }
    }

    // A flipped bit in the body or the header drops that frame, not the next one.
    {
        std::string bad_body = encode(msg("A-5", "DAMAGED BODY"));
        bad_body[bad_body.size() - 4] ^= 0x01;
        std::string bad_len = encode(msg("A-6", "DAMAGED LENGTH"));
        size_t sp = bad_len.find(" 14 ");
        bad_len[sp + 1] = '9';  // length 94: header check must catch it
        FrameParser p;
        auto got = p.feed(bad_body + bad_len + encode(msg("A-7", "GOOD")));
        CHECK(got.size() == 1 && got[0].id == "A-7");
        CHECK(p.dropped() >= 2);
    }

    // Precedence order.
    CHECK(prec_rank('Z') < prec_rank('O'));
    CHECK(prec_rank('O') < prec_rank('P'));
    CHECK(prec_rank('P') < prec_rank('R'));
    CHECK(!valid_prec('X'));
    CHECK(valid_station("AAM402"));
    CHECK(!valid_station("aam402"));
    CHECK(!valid_station(""));
    CHECK(!valid_station("A B"));

    // JSON request parsing.
    {
        std::map<std::string, std::string> r;
        CHECK(parse_flat_object(R"({"cmd":"send","to":"B","body":"LINE 1\nSAID \"HI\" é","limit":5,"x":true})", r));
        CHECK(r["cmd"] == "send");
        CHECK(r["body"] == "LINE 1\nSAID \"HI\" \xc3\xa9");
        CHECK(r["limit"] == "5");
        CHECK(r["x"] == "true");
        std::map<std::string, std::string> bad;
        CHECK(!parse_flat_object("{\"a\":[1,2]}", bad));
        CHECK(!parse_flat_object("not json", bad));
        CHECK(!parse_flat_object("{\"a\":\"b\"} trailing", bad));
    }
    {
        JsonWriter w;
        w.begin_object().field("a", "q\"\n").begin_array("l");
        w.begin_object().field("n", 1).end_object();
        w.begin_object().field("n", 2).end_object();
        w.end_array().field("t", true).end_object();
        CHECK(w.str() == R"({"a":"q\"\n","l":[{"n":1},{"n":2}],"t":true})");
    }

    return check::report("test_frame");
}
