/*
 *  The JSON writer is checked by parsing what it emits, not by comparing
 *  strings: the test should fail when the output is *wrong*, not when it is
 *  merely spelled differently. Commas and escaping are the two things a
 *  hand-written writer gets wrong, so both are pushed at deliberately.
 */
#include "json.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace {

std::string     emit(auto&& body)
{
    std::ostringstream  os;
    json::Writer        w(os);
    body(w);
    return os.str();
}

} // namespace

TEST(Json, Scalars)
{
    EXPECT_EQ(emit([](auto& w){ w.value(true); }),                  "true");
    EXPECT_EQ(emit([](auto& w){ w.value(std::int64_t(-7)); }),      "-7");
    EXPECT_EQ(emit([](auto& w){ w.null(); }),                       "null");
    EXPECT_EQ(emit([](auto& w){ w.value("hi"); }),                  "\"hi\"");
}

//  The classic failure: a comma before the first item, or none between two.
TEST(Json, CommasArePlacedByStructureNotByCare)
{
    EXPECT_EQ(emit([](auto& w){ auto a = w.array(); }), "[]");

    EXPECT_EQ(emit([](auto& w){
        auto    a = w.array();
        w.value(1).value(2).value(3);
    }), "[1,2,3]");

    EXPECT_EQ(emit([](auto& w){
        auto    o = w.object();
        w.key("a").value(1);
        w.key("b").value(2);
    }), "{\"a\":1,\"b\":2}");

    //  A key places the comma, so the value after it must not place another.
    EXPECT_EQ(emit([](auto& w){
        auto    o = w.object();
        w.key("only").value(std::int64_t(1));
    }), "{\"only\":1}");
}

TEST(Json, NestingClosesItself)
{
    auto    text = emit([](auto& w){
        auto    o = w.object();
        w.key("kind").value("signal");
        w.key("at");
        {
            auto    a = w.array();
            w.value(0).value(3);
        }
        w.key("nested");
        {
            auto    n = w.object();
            w.key("deep");
            {
                auto    d = w.array();
                {   auto e = w.object(); w.key("x").value(1);   }
                {   auto e = w.object(); w.key("y").value(2);   }
            }
        }
    });

    EXPECT_EQ(text, "{\"kind\":\"signal\",\"at\":[0,3],"
                    "\"nested\":{\"deep\":[{\"x\":1},{\"y\":2}]}}");
}

//  Signal names come from CSV headers and paths from a command line, so the
//  input is not under the writer's control.
TEST(Json, EscapesWhatMustBeEscaped)
{
    EXPECT_EQ(emit([](auto& w){ w.value("a\"b"); }),        "\"a\\\"b\"");
    EXPECT_EQ(emit([](auto& w){ w.value("a\\b"); }),        "\"a\\\\b\"");
    EXPECT_EQ(emit([](auto& w){ w.value("a\nb"); }),        "\"a\\nb\"");
    EXPECT_EQ(emit([](auto& w){ w.value("a\tb"); }),        "\"a\\tb\"");
    EXPECT_EQ(emit([](auto& w){ w.value(std::string("a\x01""b")); }), "\"a\\u0001b\"");

    //  UTF-8 passes through byte for byte; the writer does not need to
    //  understand it to keep it intact.
    EXPECT_EQ(emit([](auto& w){ w.value("µs"); }),          "\"\xc2\xb5s\"");
}

//  JSON has no infinity or NaN. null at least says "no value" rather than
//  emitting something no parser will accept.
TEST(Json, NonFiniteNumbersBecomeNull)
{
    EXPECT_EQ(emit([](auto& w){ w.value(0.0 / 0.0); }),     "null");
    EXPECT_EQ(emit([](auto& w){ w.value(1.0 / 0.0); }),     "null");
}

TEST(Json, NewlineDelimitedRecords)
{
    auto    text = emit([](auto& w){
        {   auto o = w.object(); w.key("kind").value("header");   }
        w.line();
        {   auto o = w.object(); w.key("kind").value("signal");   }
        w.line();
    });

    EXPECT_EQ(text, "{\"kind\":\"header\"}\n{\"kind\":\"signal\"}\n");
}
