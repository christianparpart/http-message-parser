// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sys/types.h> // ssize_t

#include <string_view>

enum class HttpVersion
{
    UNKNOWN = 0,
    VERSION_0_9 = 9,
    VERSION_1_0 = 10,
    VERSION_1_1 = 11,
};

enum class HttpStatus // {{{
{
    Undefined = 0,

    // informational
    ContinueRequest = 100,
    SwitchingProtocols = 101,
    Processing = 102, // WebDAV, RFC 2518

    // successful
    Ok = 200,
    Created = 201,
    Accepted = 202,
    NonAuthoriativeInformation = 203,
    NoContent = 204,
    ResetContent = 205,
    PartialContent = 206,

    // redirection
    MultipleChoices = 300,
    MovedPermanently = 301,
    Found = 302,
    MovedTemporarily = Found,
    NotModified = 304,
    TemporaryRedirect = 307, // since HTTP/1.1
    PermanentRedirect = 308, // Internet-Draft

    // client error
    BadRequest = 400,
    Unauthorized = 401,
    PaymentRequired = 402, // reserved for future use
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    NotAcceptable = 406,
    ProxyAuthenticationRequired = 407,
    RequestTimeout = 408,
    Conflict = 409,
    Gone = 410,
    LengthRequired = 411,
    PreconditionFailed = 412,
    PayloadTooLarge = 413,
    RequestUriTooLong = 414,
    UnsupportedMediaType = 415,
    RequestedRangeNotSatisfiable = 416,
    ExpectationFailed = 417,
    MisdirectedRequest = 421,
    UnprocessableEntity = 422,
    Locked = 423,
    FailedDependency = 424,
    UnorderedCollection = 425,
    UpgradeRequired = 426,
    PreconditionRequired = 428,        // RFC 6585
    TooManyRequests = 429,             // RFC 6585
    RequestHeaderFieldsTooLarge = 431, // RFC 6585
    NoResponse = 444,                  // nginx ("Used in Nginx logs to indicate that the server
                                       // has returned no information to the client and closed the
                                       // connection")
    Hangup = 499,                      // Used in Nginx to indicate that the client has aborted the
                                       // connection before the server could serve the response.

    // server error
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504,
    HttpVersionNotSupported = 505,
    VariantAlsoNegotiates = 506,        // RFC 2295
    InsufficientStorage = 507,          // WebDAV, RFC 4918
    LoopDetected = 508,                 // WebDAV, RFC 5842
    BandwidthExceeded = 509,            // Apache
    NotExtended = 510,                  // RFC 2774
    NetworkAuthenticationRequired = 511 // RFC 6585
};
// }}}

class HttpListener // {{{
{
  public:
    virtual ~HttpListener() = default;

    /** HTTP/1.1 Request-Line, that has been fully parsed.
     *
     * @param method the request-method (e.g. GET or POST)
     * @param entity the requested URI (e.g. /index.html)
     * @param version HTTP version (e.g. 0.9 or 2.0)
     */
    virtual void onMessageBegin(std::string_view method, std::string_view entity, HttpVersion version) {}

    /** HTTP/1.1 response Status-Line, that has been fully parsed.
     *
     * @param version HTTP version (e.g. 0.9 or 2.0)
     * @param code HTTP response status code (e.g. 200 or 404)
     * @param text HTTP response status text (e.g. "Ok" or "Not Found")
     */
    virtual void onMessageBegin(HttpVersion version, HttpStatus code, std::string_view text) {}

    /**
     * HTTP generic message begin (neither request nor response message).
     */
    virtual void onMessageBegin() {}

    /**
     * Single HTTP message header.
     *
     * @param name the header name
     * @param value the header value
     *
     * @note Does nothing but returns true by default.
     */
    virtual void onMessageHeader(std::string_view name, std::string_view value) {}

    /**
     * Invoked once all request headers have been fully parsed.
     *
     * (no possible content parsed yet)
     *
     * @note Does nothing but returns true by default.
     */
    virtual void onMessageHeaderEnd() {}

    /**
     * Invoked for every chunk of message content being processed.
     *
     * @note Does nothing but returns true by default.
     */
    virtual void onMessageContent(std::string_view chunk) {}

    /**
     * Invoked once a fully HTTP message has been processed.
     *
     * @note Does nothing but returns true by default.
     */
    virtual void onMessageEnd() {}

    /**
     * HTTP message protocol/transport error.
     */
    virtual void onProtocolError() {}
}; // }}}

enum class HttpParserState // {{{
{
    // artificial
    PROTOCOL_ERROR = 1,
    MESSAGE_BEGIN,

    // Request-Line
    REQUEST_LINE_BEGIN = 100,
    REQUEST_METHOD,
    REQUEST_ENTITY_BEGIN,
    REQUEST_ENTITY,
    REQUEST_PROTOCOL_BEGIN,
    REQUEST_PROTOCOL_T1,
    REQUEST_PROTOCOL_T2,
    REQUEST_PROTOCOL_P,
    REQUEST_PROTOCOL_SLASH,
    REQUEST_PROTOCOL_VERSION_MAJOR,
    REQUEST_PROTOCOL_VERSION_MINOR,
    REQUEST_LINE_LF,
    REQUEST_0_9_LF,

    // Status-Line
    STATUS_LINE_BEGIN = 150,
    STATUS_PROTOCOL_BEGIN,
    STATUS_PROTOCOL_T1,
    STATUS_PROTOCOL_T2,
    STATUS_PROTOCOL_P,
    STATUS_PROTOCOL_SLASH,
    STATUS_PROTOCOL_VERSION_MAJOR,
    STATUS_PROTOCOL_VERSION_MINOR,
    STATUS_CODE_BEGIN,
    STATUS_CODE,
    STATUS_MESSAGE_BEGIN,
    STATUS_MESSAGE,
    STATUS_MESSAGE_LF,

    // message-headers
    HEADER_NAME_BEGIN = 200,
    HEADER_NAME,
    HEADER_COLON,
    HEADER_VALUE_BEGIN,
    HEADER_VALUE,
    HEADER_VALUE_LF,
    HEADER_VALUE_END,
    HEADER_END_LF,

    // LWS ::= [CR LF] 1*(SP | HT)
    LWS_BEGIN = 300,
    LWS_LF,
    LWS_SP_HT_BEGIN,
    LWS_SP_HT,

    // message-content
    CONTENT_BEGIN = 400,
    CONTENT,
    CONTENT_ENDLESS = 405,
    CONTENT_CHUNK_SIZE_BEGIN = 410,
    CONTENT_CHUNK_SIZE,
    CONTENT_CHUNK_LF1,
    CONTENT_CHUNK_BODY,
    CONTENT_CHUNK_LF2,
    CONTENT_CHUNK_CR3,
    CONTENT_CHUNK_LF3
}; // }}}

enum class HttpParseMode
{
    /// the message to parse does not contain either an HTTP request-line nor
    /// response status-line but headers and a body.
    MESSAGE,

    /// the message to parse is an HTTP request.
    REQUEST,

    /// the message to parse is an HTTP response.
    RESPONSE,
};

class HttpParser
{
  public:
    ///
    /// Initializes the HTTP/1.1 message processor.
    ///
    /// @param mode REQUEST: parses and processes an HTTP/1.1 Request,
    ///             RESPONSE: parses and processes an HTTP/1.1 Response.
    ///             MESSAGE: parses and processes an HTTP/1.1 message, that is,
    ///             without the first request/status line - just headers and
    ///             content.
    ///
    /// @param listener an HttpListener for receiving HTTP message events.
    ///
    /// @note No member variable may be modified after the hook invokation
    ///       returned with a false return code, which means, that processing
    ///       is to be cancelled and thus, may imply, that the object itself
    ///       may have been already deleted.
    HttpParser(HttpParseMode mode, HttpListener* listener) noexcept;

    ///
    /// Processes a message-chunk.
    ///
    /// @param chunk the chunk of bytes to process
    /// @return      number of bytes actually parsed and processed
    size_t parseFragment(std::string_view chunk) noexcept;

    ssize_t contentLength() const noexcept;
    bool isChunked() const noexcept { return _chunked; }
    void reset() noexcept;
    bool isProcessingHeader() const noexcept;
    bool isProcessingBody() const noexcept;

    bool isContentExpected() const noexcept
    {
        return _contentLength > 0 || _chunked || (_contentLength < 0 && _mode != HttpParseMode::REQUEST);
    }

    size_t bytesReceived() const noexcept { return _bytesReceived; }

  private:
    HttpParseMode _mode;                                     /// parsing mode (request/response/something)
    HttpListener* _listener;                                 /// HTTP message component listener
    HttpParserState _state = HttpParserState::MESSAGE_BEGIN; /// the current parser/processing state

    // stats
    size_t _bytesReceived = 0;

    // implicit LWS handling
    HttpParserState _lwsNext; //!< state to apply on successfull LWS
    HttpParserState _lwsNull; //!< state to apply on (CR LF) but no 1*(SP | HT)

    // request-line
    std::string_view _method; //!< HTTP request method
    std::string_view _entity; //!< HTTP request entity
    int _versionMajor {};     //!< HTTP request/response version major
    int _versionMinor {};     //!< HTTP request/response version minor

    // status-line
    int _code = 0;             //!< response status code
    std::string_view _message; //!< response status message

    // current parsed header
    std::string_view _name;
    std::string_view _value;

    // body
    bool _chunked = false;       //!< whether or not request content is chunked encoded
    ssize_t _contentLength = -1; //!< content length of whole content or current chunk
};
