/**
 * @file RestCodecTests.cpp
 * @brief Verifies REST JSON <-> model translation: field mapping and the error
 *        contract ({ "error", "message" } + HTTP status). Uses the real codec so
 *        parsing stays byte-identical to the shipped behavior.
 */

#include "test_framework.h"

#include <idtxflow/net/adapters/rest/RestCodec.h>

using idtxflow::net::adapters::RestCodec;

TEST(RestCodec, ParseLoginFields)
{
    idtxflow::net::model::LoginResult lr;
    const bool ok = RestCodec::parse_login(
        R"({"access_token":"abc","token_type":"Bearer","expires_in":3600,"scope":"read"})", lr);
    CHECK(ok);
    CHECK_EQ(lr.access_token, std::string("abc"));
    CHECK_EQ(lr.expires_in, static_cast<int64_t>(3600));
    CHECK_EQ(lr.scope, std::string("read"));
}

TEST(RestCodec, ParseLoginMissingTokenFails)
{
    idtxflow::net::model::LoginResult lr;
    CHECK(!RestCodec::parse_login(R"({"token_type":"Bearer"})", lr));
}

TEST(RestCodec, ParseSessionFields)
{
    idtxflow::net::model::SessionInfo si;
    const bool ok = RestCodec::parse_session(
        R"({"session_id":"s1","usd_file":"scenes/a.usda","mode":"single_edit","ws_url":"/ws?sid=s1"})", si);
    CHECK(ok);
    CHECK_EQ(si.session_id, std::string("s1"));
    CHECK_EQ(si.usd_file, std::string("scenes/a.usda"));
    CHECK_EQ(si.ws_url, std::string("/ws?sid=s1"));
}

TEST(RestCodec, ParseErrorBody)
{
    // A structured error body maps error/message; the HTTP status is preserved.
    const auto e = RestCodec::parse_error(400, R"({"error":"bad_request","message":"nope"})", "");
    CHECK_EQ(e.http_code, 400);
    CHECK_EQ(e.error_code, std::string("bad_request"));
    CHECK_EQ(e.message, std::string("nope"));

    // A transport failure (status 0) uses the transport error text.
    const auto t = RestCodec::parse_error(0, "", "connection refused");
    CHECK_EQ(t.http_code, 0);
    CHECK_EQ(t.error_code, std::string("transport_error"));
    CHECK_EQ(t.message, std::string("connection refused"));

    // A non-JSON body falls back to a synthesized code and the raw body.
    const auto f = RestCodec::parse_error(500, "boom", "");
    CHECK_EQ(f.error_code, std::string("http_500"));
    CHECK_EQ(f.message, std::string("boom"));
}

TEST(RestCodec, ParseFilesNormalizesPaths)
{
    // The backend may send Windows separators and a stray leading slash; the
    // session and download contract expects forward slashes with no leading
    // slash on the file path. The directory keeps its separators normalized but
    // is not leading-slash-stripped (it is display/sort metadata).
    std::vector<idtxflow::net::model::FileEntry> files;
    const bool ok = RestCodec::parse_files(
        R"({"files":[
            {"filepath":"/Teapot\\geo\\lid.usda","filename":"lid.usda","directory":"Teapot\\geo"},
            {"filepath":"root.usda","filename":"root.usda","directory":""}
        ]})", files);
    CHECK(ok);
    CHECK_EQ(files.size(), static_cast<size_t>(2));
    CHECK_EQ(files[0].filepath, std::string("Teapot/geo/lid.usda"));
    CHECK_EQ(files[0].directory, std::string("Teapot/geo"));
    CHECK_EQ(files[1].filepath, std::string("root.usda"));
    CHECK_EQ(files[1].directory, std::string(""));
}

TEST(RestCodec, ParseFilesDecodesModifiedEpoch)
{
    // `modified` is an implementation-defined count; the codec collapses it toward
    // Unix seconds and accepts any value from ~1970 onward (at least one day past
    // the epoch), reporting 0 only for degenerate near-zero counts. The raw value
    // is preserved for stable sorting.
    std::vector<idtxflow::net::model::FileEntry> files;
    const bool ok = RestCodec::parse_files(
        R"({"files":[
            {"filepath":"a","filename":"a","modified":1700000000},
            {"filepath":"b","filename":"b","modified":1700000000000},
            {"filepath":"c","filename":"c","modified":1700000000000000000},
            {"filepath":"d","filename":"d","modified":13412096083408043},
            {"filepath":"e","filename":"e","modified":86400},
            {"filepath":"f","filename":"f","modified":86399},
            {"filepath":"g","filename":"g","modified":42},
            {"filepath":"h","filename":"h","modified":0}
        ]})", files);
    CHECK(ok);
    CHECK_EQ(files.size(), static_cast<size_t>(8));
    // Seconds pass through; millis and nanos collapse to the same second.
    CHECK_EQ(files[0].modified_epoch, static_cast<int64_t>(1700000000));
    CHECK_EQ(files[1].modified_epoch, static_cast<int64_t>(1700000000));
    CHECK_EQ(files[2].modified_epoch, static_cast<int64_t>(1700000000));
    // A real libstdc++ nanoseconds-since-epoch value decodes to a mid-1970 second.
    CHECK_EQ(files[3].modified_epoch, static_cast<int64_t>(13412096));
    // Boundary: one day past the epoch is accepted; below it is uninterpretable.
    CHECK_EQ(files[4].modified_epoch, static_cast<int64_t>(86400));
    CHECK_EQ(files[5].modified_epoch, static_cast<int64_t>(0));
    CHECK_EQ(files[6].modified_epoch, static_cast<int64_t>(0));
    CHECK_EQ(files[7].modified_epoch, static_cast<int64_t>(0));
    // The raw value is retained regardless.
    CHECK_EQ(files[1].modified, static_cast<int64_t>(1700000000000));
    CHECK_EQ(files[3].modified, static_cast<int64_t>(13412096083408043));
}
