// Host tests for pico_mjson.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "pico_mjson.h"

static std::string str(const JsonView& view)
{
    return std::string(view.data(), view.length());
}

static std::string get(const JsonView& view, const char *path)
{
    char out[64];
    return view.get_string(path, out, sizeof(out)) ? std::string(out) : std::string("<none>");
}

// A getUpdates answer as telegram sends it: non-ASCII escaped, ids numbers.
static const char *UPDATES = R"({"ok":true,"result":[
  {"update_id":815234567,
   "message":{"message_id":12,"chat":{"id":-1001234567890,"type":"group"},"text":"/water/1/10 și 🌱"}},
  {"update_id":815234568,
   "callback_query":{"id":"4382bfdwdsb323b2d9","data":"/stop/0",
                     "message":{"chat":{"id":123456789}}}}
]})";

int main()
{
    printf("reading by path\n");
    {
        JsonView root(UPDATES);

        assert(root.is_valid());
        assert(root.type() == JsonType::Object);
        assert(root.type("$.ok") == JsonType::Bool);
        assert(root.type("$.result") == JsonType::Array);
        assert(root.type("$.result[0].message") == JsonType::Object);
        assert(root.type("$.result[5]") == JsonType::Missing);
        assert(!root.has("$.nope"));

        bool ok = false;
        assert(root.get_bool("$.ok", ok) && ok);

        uint32_t id = 0;
        assert(root.get_uint("$.result[1].update_id", id) && (id == 815234568));

        assert(get(root, "$.result[1].callback_query.data") == "/stop/0");
        assert(get(root, "$.result[0].update_id") == "815234567");   // numbers come as written
        assert(get(root, "$.result[0].message") == "<none>");          // objects do not
    }

    printf("unescaping\n");
    {
        JsonView root(UPDATES);

        // U+0219 and a surrogate pair, which mjson_get_string alone refuses.
        assert(get(root, "$.result[0].message.text") == "/water/1/10 \xC8\x99i \xF0\x9F\x8C\xB1");

        JsonView escapes(R"({"a":"q\"b\\s\/n\nt\tuA","bad":"x\q","lone":"\ud83cz"})");
        assert(get(escapes, "$.a") == "q\"b\\s/n\nt\tuA");
        assert(get(escapes, "$.lone") == "\xEF\xBF\xBDz");
        assert(get(escapes, "$.bad") == "<none>");
    }

    printf("cut off at a character boundary\n");
    {
        JsonView root(UPDATES);
        char out[16];

        // 20 bytes in all; 15 fit, which ends right after the "i".
        assert(root.get_string("$.result[0].message.text", out, sizeof(out)));
        assert(strcmp(out, "/water/1/10 \xC8\x99i") == 0);

        char small[14];
        assert(root.get_string("$.result[0].message.text", small, sizeof(small)));
        assert(strcmp(small, "/water/1/10 ") == 0);   // ș would be split, so dropped

        assert(!root.get_string("$.result[0].message.text", NULL, 0));

        assert(!root.get_whole_string("$.result[0].message.text", out, sizeof(out)) && (out[0] == 0));

        char whole[21];   // 20 bytes and the terminator
        assert(!root.get_whole_string("$.result[0].message.text", whole, 20) && (whole[0] == 0));
        assert(root.get_whole_string("$.result[0].message.text", whole, sizeof(whole)));
        assert(strcmp(whole, "/water/1/10 \xC8\x99i \xF0\x9F\x8C\xB1") == 0);
    }

    printf("ids as text\n");
    {
        JsonView root(UPDATES);
        char id[32];

        assert(root.get_string("$.result[0].message.chat.id", id, sizeof(id)));
        assert(strcmp(id, "-1001234567890") == 0);
        assert(root.get_string("$.result[1].callback_query.id", id, sizeof(id)));
        assert(strcmp(id, "4382bfdwdsb323b2d9") == 0);
        assert(root.get_string("$.ok", id, sizeof(id)) && strcmp(id, "true") == 0);
        assert(!root.get_string("$.result", id, sizeof(id)) && (id[0] == 0));
    }

    printf("integers\n");
    {
        JsonView numbers(R"({"big":-1001234567890,"max":9223372036854775807,"min":-9223372036854775808,
                             "over":9223372036854775808,"frac":1.5,"exp":1e3,"u32":4294967295,"u33":4294967296,"neg":-1})");
        int64_t v = 0;
        uint32_t u = 0;
        double d = 0;

        assert(numbers.get_int("$.big", v) && (v == -1001234567890LL));
        assert(numbers.get_int("$.max", v) && (v == INT64_MAX));
        assert(numbers.get_int("$.min", v) && (v == INT64_MIN));
        assert(!numbers.get_int("$.over", v));
        assert(!numbers.get_int("$.frac", v));
        assert(!numbers.get_int("$.exp", v));
        assert(numbers.get_uint("$.u32", u) && (u == 4294967295u));
        assert(!numbers.get_uint("$.u33", u));
        assert(!numbers.get_uint("$.neg", u));
        assert(numbers.get_number("$.frac", d) && (d == 1.5));
        assert(numbers.get_number("$.exp", d) && (d == 1000));
    }

    printf("iterating\n");
    {
        JsonView root(UPDATES);
        uint32_t count = 0;

        for (const JsonEntry& entry : root.find("$.result"))
        {
            assert(entry.key.length() == 0);
            assert(entry.value.type() == JsonType::Object);
            ++count;
        }
        assert(count == 2);

        std::string keys;
        for (const JsonEntry& entry : JsonView(R"({"a":1,"b!":[1,2],"c":{"d":2}})"))
        {
            char key[8];
            entry.key.get_string("$", key, sizeof(key));
            keys += key;
            keys += "=" + str(entry.value) + ";";
        }
        assert(keys == "a=1;b!=[1,2];c={\"d\":2};");

        JsonView object(R"({"ok":true,"why":"x"})");
        JsonIterator it = object.begin();
        assert(it->key.equals("ok") && !it->key.equals("o") && !it->key.equals("okk"));
        assert(it->key.starts_with("o") && it->key.starts_with("ok") && it->key.starts_with("") && !it->key.starts_with("okk") && !it->key.starts_with("k"));
        ++it;
        assert(it->key.equals("why") && (str(it->value) == "\"x\""));
        JsonView why_key = it->key;
        ++it;
        assert(it == object.end());

        assert(str(object.member(why_key)) == "\"x\"");
        assert(object.member(JsonView("\"wh\"")).data() == NULL);
        assert(JsonView("[1]").member(why_key).data() == NULL);

        count = 0;
        for (const JsonEntry& entry : JsonView("[]")) { (void)entry; ++count; }
        for (const JsonEntry& entry : JsonView("5")) { (void)entry; ++count; }
        for (const JsonEntry& entry : JsonView()) { (void)entry; ++count; }
        assert(count == 0);
    }

    printf("not json\n");
    {
        JsonView broken(R"({"ok":tru)");
        bool ok = true;

        assert(!broken.is_valid());
        assert(!JsonView().is_valid());
        assert(broken.type("$.ok") == JsonType::Missing);
        assert(!JsonView().get_bool("$.ok", ok));
        assert(get(JsonView(), "$.a") == "<none>");
    }

    printf("writer\n");
    {
        static const char *BUTTONS[][2] = { { "10m", "/water/1/10" }, { "OFF", "/stop/1" }, { "Say \"hi\"", "/hi" } };

        char body[512];
        JsonWriter json(body, sizeof(body));

        json.begin_object()
                .add_string("chat_id", "-100123")
                .add_string("text", "line\nnext \x01 \xC8\x99")
                .begin_object("reply_markup")
                    .begin_array("inline_keyboard");

        for (uint32_t row = 0; row < 2; ++row)
        {
            json.begin_array();
            for (uint32_t i = row * 2; (i < row * 2 + 2) && (i < 3); ++i)
            {
                json.begin_object()
                        .add_string("text", BUTTONS[i][0])
                        .add_string("callback_data", BUTTONS[i][1])
                    .end_object();
            }
            json.end_array();
        }

        json.end_array()
            .end_object()
            .add_int("min", INT64_MIN)
            .add_uint("u", 42)
            .add_number("pi", 3.25)
            .add_bool("yes", true)
            .add_null("none")
            .add_string("null_value", NULL)
            .add_raw("raw", JsonView("[1,{\"a\":2}]"))
            .add_fstring("f", "valve %d: \"%s\"\n", 2, "open")
            .end_object();

        assert(json.ok());
        assert(json.length() == strlen(body));

        const char *expected =
            "{\"chat_id\":\"-100123\",\"text\":\"line\\nnext \\u0001 \xC8\x99\","
            "\"reply_markup\":{\"inline_keyboard\":[[{\"text\":\"10m\",\"callback_data\":\"/water/1/10\"},"
            "{\"text\":\"OFF\",\"callback_data\":\"/stop/1\"}],[{\"text\":\"Say \\\"hi\\\"\",\"callback_data\":\"/hi\"}]]},"
            "\"min\":-9223372036854775808,\"u\":42,\"pi\":3.25,\"yes\":true,\"none\":null,\"null_value\":null,"
            "\"raw\":[1,{\"a\":2}],\"f\":\"valve 2: \\\"open\\\"\\n\"}";

        if (strcmp(body, expected) != 0)
        {
            printf("got      %s\nexpected %s\n", body, expected);
            assert(false);
        }

        // And it reads back.
        JsonView back(body, json.length());
        assert(back.is_valid());
        assert(get(back, "$.reply_markup.inline_keyboard[1][0].text") == "Say \"hi\"");
        assert(get(back, "$.text") == "line\nnext \x01 \xC8\x99");
        int64_t v = 0;
        assert(back.get_int("$.min", v) && (v == INT64_MIN));
    }

    printf("writer and UTF-8\n");
    {
        char body[128];

        // Valid UTF-8 passes as it is: two, three and four byte sequences.
        JsonWriter good(body, sizeof(body));
        good.add_string(NULL, "\xC8\x99 \xE2\x82\xAC \xF0\x9F\x8C\xB1");
        assert(good.ok() && (strcmp(body, "\"\xC8\x99 \xE2\x82\xAC \xF0\x9F\x8C\xB1\"") == 0));

        // A stray continuation, a lead cut off, an overlong '/', a surrogate,
        // and 0xff: each bad byte becomes U+FFFD.
        JsonWriter bad(body, sizeof(body));
        bad.add_string(NULL, "a\x80" "b\xC8" "c\xC0\xAF" "d\xED\xA0\x80" "e\xff");
        assert(bad.ok());
        assert(strcmp(body, "\"a\\ufffdb\\ufffdc\\ufffd\\ufffdd\\ufffd\\ufffd\\ufffde\\ufffd\"") == 0);

        // Cut off at the end of the text, and the same through add_fstring.
        JsonWriter cut(body, sizeof(body));
        cut.add_fstring(NULL, "%s", "x\xE2\x82");
        assert(cut.ok() && (strcmp(body, "\"x\\ufffd\\ufffd\"") == 0));

        JsonWriter formatted(body, sizeof(body));
        formatted.add_fstring(NULL, "%s \xff", "\xF0\x9F\x8C\xB1");
        assert(formatted.ok() && (strcmp(body, "\"\xF0\x9F\x8C\xB1 \\ufffd\"") == 0));

        // It reads back as what was meant.
        char out[32];
        assert(JsonView(body, formatted.length()).get_string("$", out, sizeof(out)));
        assert(strcmp(out, "\xF0\x9F\x8C\xB1 \xEF\xBF\xBD") == 0);
    }

    printf("writer out of room\n");
    {
        char small[16];
        JsonWriter json(small, sizeof(small));
        json.begin_object().add_string("text", "this does not fit").end_object();

        assert(!json.ok());
        assert(json.length() < sizeof(small));
        assert(strlen(small) == json.length());

        JsonWriter none(NULL, 0);
        none.add_null(NULL);
        assert(!none.ok() && (none.length() == 0));
    }

    printf("add_fstring at the edge\n");
    {
        // "\"abc\"" is 5 bytes and the NUL makes 6: exactly fits, one less does not.
        for (uint32_t size = 4; size <= 8; ++size)
        {
            char buffer[8];
            memset(buffer, 'X', sizeof(buffer));
            JsonWriter json(buffer, size);
            json.add_fstring(NULL, "%s", "abc");

            assert(json.ok() == (size >= 6));
            assert(strlen(buffer) == json.length());
            if (json.ok())
            {
                assert(strcmp(buffer, "\"abc\"") == 0);
            }
        }

        // Every character escaped, 2 bytes each: "\"\\n\\n\\n\"" is 8 bytes, needs 9.
        for (uint32_t size = 5; size <= 12; ++size)
        {
            char buffer[12];
            JsonWriter json(buffer, size);
            json.add_fstring(NULL, "\n\n\n");

            assert(json.ok() == (size >= 9));
            assert(strlen(buffer) == json.length());
            if (json.ok())
            {
                assert(strcmp(buffer, "\"\\n\\n\\n\"") == 0);
            }
        }

        // \x01 grows six times, so the formatted text fitting is not enough.
        char buffer[16];
        JsonWriter grow(buffer, sizeof(buffer));
        grow.add_fstring(NULL, "%s", "\x01\x01\x01");
        assert(!grow.ok());
        assert(strlen(buffer) == grow.length());

        // After other values, in an object.
        char body[64];
        JsonWriter json(body, sizeof(body));
        json.begin_object().add_int("a", 1).add_fstring("b", "%d%%", 50).add_fstring("c", "%s", "").end_object();
        assert(json.ok());
        assert(strcmp(body, "{\"a\":1,\"b\":\"50%\",\"c\":\"\"}") == 0);
    }

    printf("writer misuse\n");
    {
        char body[64];

        JsonWriter unclosed(body, sizeof(body));
        unclosed.begin_object().add_int("a", 1);
        assert(!unclosed.ok());

        JsonWriter no_key(body, sizeof(body));
        no_key.begin_object().add_int(NULL, 1).end_object();
        assert(!no_key.ok());

        JsonWriter key_in_array(body, sizeof(body));
        key_in_array.begin_array().add_int("a", 1).end_array();
        assert(!key_in_array.ok());

        JsonWriter crossed(body, sizeof(body));
        crossed.begin_object().end_array();
        assert(!crossed.ok());

        JsonWriter two(body, sizeof(body));
        two.add_int(NULL, 1).add_int(NULL, 2);
        assert(!two.ok());

        JsonWriter scalar(body, sizeof(body));
        scalar.add_string(NULL, "just a string");
        assert(scalar.ok() && (strcmp(body, "\"just a string\"") == 0));
    }

    {
        // same_string: in place, missing and null count as "".
        JsonView doc(R"({"a":"x\"y","n":null,"num":5})");
        assert(doc.same_string("$.a", "x\"y"));
        assert(!doc.same_string("$.a", "x"));
        assert(doc.same_string("$.missing", ""));
        assert(doc.same_string("$.n", ""));
        assert(!doc.same_string("$.missing", "x"));
        assert(!doc.same_string("$.num", "5"));
        printf("same_string\n");
    }

    {
        // key(): a key token from an iterator, copied as it is.
        JsonView doc(R"({"pl\u0061in":1,"esc\"aped":{"x":true}})");
        char buffer[128];
        JsonWriter writer(buffer, sizeof(buffer));
        writer.begin_object();
        for (const JsonEntry& entry : doc)
        {
            writer.key(entry.key).add_raw(NULL, entry.value);
        }
        writer.end_object();
        assert(writer.ok());
        assert(std::string(writer.c_str()) == R"({"pl\u0061in":1,"esc\"aped":{"x":true}})");

        JsonWriter nested(buffer, sizeof(buffer));
        nested.begin_object();
        for (const JsonEntry& entry : doc)
        {
            nested.key(entry.key).begin_object().end_object();
        }
        nested.end_object();
        assert(nested.ok());
        assert(std::string(nested.c_str()) == R"({"pl\u0061in":{},"esc\"aped":{}})");

        // Not a string token, or a key at the top level: refused.
        JsonWriter bad(buffer, sizeof(buffer));
        bad.begin_object().key(JsonView("12")).add_int(NULL, 1).end_object();
        assert(!bad.ok());
        JsonWriter top(buffer, sizeof(buffer));
        top.key(JsonView("\"k\"")).add_int(NULL, 1);
        assert(!top.ok());
        printf("raw keys\n");
    }

    printf("all passed\n");
    return 0;
}
