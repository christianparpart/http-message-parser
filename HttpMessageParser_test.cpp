// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_all.hpp>

#include "HttpMessageParser.h"

class MockHttpListener: public HttpListener
{ // {{{
  public:
    void onMessageBegin(std::string_view method, std::string_view entity, HttpVersion version) override;
    void onMessageBegin(HttpVersion version, HttpStatus code, std::string_view text) override;
    void onMessageBegin() override;
    void onMessageHeader(std::string_view name, std::string_view value) override;
    void onMessageHeaderEnd() override;
    void onMessageContent(std::string_view chunk) override;
    void onMessageEnd() override;
    void onProtocolError() override;

  public:
    std::string method;
    std::string entity;
    HttpVersion version = HttpVersion::UNKNOWN;
    HttpStatus statusCode = HttpStatus::Undefined;
    std::string statusReason;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    HttpStatus errorCode = HttpStatus::Undefined;

    bool messageBegin = false;
    bool headerEnd = false;
    bool messageEnd = false;
};

void MockHttpListener::onMessageBegin(std::string_view method, std::string_view entity, HttpVersion version)
{
    this->method = method;
    this->entity = entity;
    this->version = version;
}

void MockHttpListener::onMessageBegin(HttpVersion version, HttpStatus code, std::string_view text)
{
    this->version = version;
    this->statusCode = code;
    this->statusReason = text;
}

void MockHttpListener::onMessageBegin()
{
    messageBegin = true;
}

void MockHttpListener::onMessageHeader(std::string_view name, std::string_view value)
{
    headers.emplace_back(std::string(name), std::string(value));
}

void MockHttpListener::onMessageHeaderEnd()
{
    headerEnd = true;
}

void MockHttpListener::onMessageContent(std::string_view chunk)
{
    body += chunk;
}

void MockHttpListener::onMessageEnd()
{
    messageEnd = true;
}

void MockHttpListener::onProtocolError()
{
    errorCode = HttpStatus::BadRequest;
}
// }}}

TEST_CASE("http_http1_Parser.requestLine0")
{
    /* Seems like in HTTP/0.9 it was possible to create
     * very simple request messages.
     */
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET /\r\n");

    REQUIRE("GET" == listener.method);
    REQUIRE("/" == listener.entity);
    REQUIRE(HttpVersion::VERSION_0_9 == listener.version);
    REQUIRE(listener.headerEnd);
    REQUIRE(listener.messageEnd);
    REQUIRE(0 == listener.headers.size());
    REQUIRE(0 == listener.body.size());
}

TEST_CASE("http_http1_Parser.requestLine1")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0.9\r\n\r\n");

    REQUIRE("GET" == listener.method);
    REQUIRE("/" == listener.entity);
    REQUIRE(HttpVersion::VERSION_0_9 == listener.version);
    REQUIRE(0 == listener.headers.size());
    REQUIRE(0 == listener.body.size());
}

TEST_CASE("http_http1_Parser.requestLine2")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("HEAD /foo?bar HTTP/1.0\r\n\r\n");

    REQUIRE("HEAD" == listener.method);
    REQUIRE("/foo?bar" == listener.entity);
    REQUIRE(HttpVersion::VERSION_1_0 == listener.version);
    REQUIRE(0 == listener.headers.size());
    REQUIRE(0 == listener.body.size());
}

TEST_CASE("http_http1_Parser.requestLine_invalid1_MissingPathAndProtoVersion")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET\r\n\r\n");
    REQUIRE(HttpStatus::BadRequest == listener.errorCode);
}

TEST_CASE("http_http1_Parser.requestLine_invalid3_InvalidVersion")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0\r\n\r\n");
    REQUIRE((int) HttpStatus::BadRequest == (int) listener.errorCode);
}

TEST_CASE("http_http1_Parser.requestLine_invalid3_CharsAfterVersion")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/1.1b\r\n\r\n");
    REQUIRE((int) HttpStatus::BadRequest == (int) listener.errorCode);
}

TEST_CASE("http_http1_Parser.requestLine_invalid5_SpaceAfterVersion")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/1.1 \r\n\r\n");
    REQUIRE((int) HttpStatus::BadRequest == (int) listener.errorCode);
}

TEST_CASE("http_http1_Parser.requestLine_invalid6_UnsupportedVersion")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);

    // Actually, we could make it a ParserError, or HttpClientError or so,
    // But to make googletest lib happy, we should make it even a distinct class.
    auto const n = parser.parseFragment("GET / HTTP/1.2\r\n\r\n");
    REQUIRE(listener.errorCode == HttpStatus::BadRequest);
}

TEST_CASE("http_http1_Parser.headers1")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::MESSAGE, &listener);
    parser.parseFragment("Foo: the foo\r\n"
                         "Content-Length: 6\r\n"
                         "\r\n"
                         "123456");

    REQUIRE("Foo" == listener.headers[0].first);
    REQUIRE("the foo" == listener.headers[0].second);
    REQUIRE("123456" == listener.body);
}

TEST_CASE("http_http1_Parser.invalidHeader1")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::MESSAGE, &listener);
    size_t n = parser.parseFragment("Foo : the foo\r\n"
                                    "\r\n");

    REQUIRE(HttpStatus::BadRequest == listener.errorCode);
    REQUIRE(3 == n);
    REQUIRE(0 == listener.headers.size());
}

TEST_CASE("http_http1_Parser.invalidHeader2")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::MESSAGE, &listener);
    size_t n = parser.parseFragment("Foo\r\n"
                                    "\r\n");

    REQUIRE(HttpStatus::BadRequest == listener.errorCode);
    REQUIRE(5 == n);
    REQUIRE(0 == listener.headers.size());
}

TEST_CASE("http_http1_Parser.requestWithHeaders")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0.9\r\n"
                         "Foo: the foo\r\n"
                         "X-Bar: the bar\r\n"
                         "\r\n");

    REQUIRE("GET" == listener.method);
    REQUIRE("/" == listener.entity);
    REQUIRE(HttpVersion::VERSION_0_9 == listener.version);
    REQUIRE(2 == listener.headers.size());
    REQUIRE(0 == listener.body.size());

    REQUIRE("Foo" == listener.headers[0].first);
    REQUIRE("the foo" == listener.headers[0].second);

    REQUIRE("X-Bar" == listener.headers[1].first);
    REQUIRE("the bar" == listener.headers[1].second);
}

TEST_CASE("http_http1_Parser.requestWithHeadersAndBody")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0.9\r\n"
                         "Foo: the foo\r\n"
                         "X-Bar: the bar\r\n"
                         "Content-Length: 6\r\n"
                         "\r\n"
                         "123456");

    REQUIRE("123456" == listener.body);
}

// no chunks except the EOS-chunk
TEST_CASE("http_http1_Parser.requestWithHeadersAndBodyChunked1")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0.9\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "\r\n"
                         "0\r\n"
                         "\r\n");

    REQUIRE("" == listener.body);
}

// exactly one data chunk
TEST_CASE("http_http1_Parser.requestWithHeadersAndBodyChunked2")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0.9\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "\r\n"

                         "6\r\n"
                         "123456"
                         "\r\n"

                         "0\r\n"
                         "\r\n");

    REQUIRE("123456" == listener.body);
}

// more than one data chunk
TEST_CASE("http_http1_Parser.requestWithHeadersAndBodyChunked3")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    parser.parseFragment("GET / HTTP/0.9\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "\r\n"

                         "6\r\n"
                         "123456"
                         "\r\n"

                         "6\r\n"
                         "123456"
                         "\r\n"

                         "0\r\n"
                         "\r\n");

    REQUIRE("123456123456" == listener.body);
}

// first chunk is missing CR LR
TEST_CASE("http_http1_Parser.requestWithHeadersAndBodyChunked_invalid1")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    size_t n = parser.parseFragment("GET / HTTP/0.9\r\n"
                                    "Transfer-Encoding: chunked\r\n"
                                    "\r\n"

                                    "6\r\n"
                                    "123456"
                                    //"\r\n" // should bailout here

                                    "0\r\n"
                                    "\r\n");

    REQUIRE(55 == n);
    REQUIRE(HttpStatus::BadRequest == listener.errorCode);
}

TEST_CASE("http_http1_Parser.pipelined1")
{
    MockHttpListener listener;
    HttpParser parser(HttpParseMode::REQUEST, &listener);
    constexpr std::string_view input = "GET /foo HTTP/1.1\r\n\r\n"
                                       "HEAD /bar HTTP/0.9\r\n\r\n";
    size_t const n = parser.parseFragment(input);

    REQUIRE("GET" == listener.method);
    REQUIRE("/foo" == listener.entity);
    REQUIRE(HttpVersion::VERSION_1_1 == listener.version);

    size_t const m = parser.parseFragment(input.substr(n));

    REQUIRE("HEAD" == listener.method);
    REQUIRE("/bar" == listener.entity);
    REQUIRE(HttpVersion::VERSION_0_9 == listener.version);

    REQUIRE(n + m == input.size());
}
