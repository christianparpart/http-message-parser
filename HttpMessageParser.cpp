// SPDX-License-Identifier: Apache-2.0
#include "HttpMessageParser.h"

#include <cassert>
#include <cctype>

namespace // helper
{

char constexpr CR = 0x0D;
char constexpr LF = 0x0A;
char constexpr SP = 0x20;
char constexpr HT = 0x09;

constexpr bool iequals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(a[i]) != std::tolower(b[i]))
            return false;
    }

    return true;
}

constexpr ssize_t parseInt(std::string_view value) noexcept
{
    ssize_t result = 0;
    for (auto const c: value)
    {
        if (std::isdigit(c))
            result = result * 10 + c - '0';
        else
            return -1;
    }

    return result;
}

constexpr bool isChar(char value) noexcept
{
    return static_cast<unsigned>(value) <= 127;
}

constexpr bool isControl(char value) noexcept
{
    return (value >= 0 && value <= 31) || value == 127;
}

constexpr bool isSeparator(char value) noexcept
{
    switch (value)
    {
        case '(':
        case ')':
        case '<':
        case '>':
        case '@':
        case ',':
        case ';':
        case ':':
        case '\\':
        case '"':
        case '/':
        case '[':
        case ']':
        case '?':
        case '=':
        case '{':
        case '}':
        case SP:
        case HT: return true;
        default: return false;
    }
}

constexpr bool isToken(char value) noexcept
{
    return isChar(value) && !(isControl(value) || isSeparator(value));
}

constexpr bool isText(char value) noexcept
{
    // TEXT = <any OCTET except CTLs but including LWS>
    return !isControl(value) || value == SP || value == HT;
}

constexpr HttpVersion makeHttpVersion(int versionMajor, int versionMinor) noexcept
{
    if (versionMajor == 0)
    {
        if (versionMinor == 9)
            return HttpVersion::VERSION_0_9;
        else
            return HttpVersion::UNKNOWN;
    }

    if (versionMajor == 1)
    {
        if (versionMinor == 0)
            return HttpVersion::VERSION_1_0;
        else if (versionMinor == 1)
            return HttpVersion::VERSION_1_1;
    }

    return HttpVersion::UNKNOWN;
}

std::string_view as_string(HttpParserState state) noexcept
{
    switch (state)
    {
        // artificial
        case HttpParserState::PROTOCOL_ERROR: return "protocol-error";
        case HttpParserState::MESSAGE_BEGIN: return "message-begin";

        // request-line
        case HttpParserState::REQUEST_LINE_BEGIN: return "request-line-begin";
        case HttpParserState::REQUEST_METHOD: return "request-method";
        case HttpParserState::REQUEST_ENTITY_BEGIN: return "request-entity-begin";
        case HttpParserState::REQUEST_ENTITY: return "request-entity";
        case HttpParserState::REQUEST_PROTOCOL_BEGIN: return "request-protocol-begin";
        case HttpParserState::REQUEST_PROTOCOL_T1: return "request-protocol-t1";
        case HttpParserState::REQUEST_PROTOCOL_T2: return "request-protocol-t2";
        case HttpParserState::REQUEST_PROTOCOL_P: return "request-protocol-p";
        case HttpParserState::REQUEST_PROTOCOL_SLASH: return "request-protocol-slash";
        case HttpParserState::REQUEST_PROTOCOL_VERSION_MAJOR: return "request-protocol-version-major";
        case HttpParserState::REQUEST_PROTOCOL_VERSION_MINOR: return "request-protocol-version-minor";
        case HttpParserState::REQUEST_LINE_LF: return "request-line-lf";
        case HttpParserState::REQUEST_0_9_LF: return "request-0-9-lf";

        // Status-Line
        case HttpParserState::STATUS_LINE_BEGIN: return "status-line-begin";
        case HttpParserState::STATUS_PROTOCOL_BEGIN: return "status-protocol-begin";
        case HttpParserState::STATUS_PROTOCOL_T1: return "status-protocol-t1";
        case HttpParserState::STATUS_PROTOCOL_T2: return "status-protocol-t2";
        case HttpParserState::STATUS_PROTOCOL_P: return "status-protocol-t2";
        case HttpParserState::STATUS_PROTOCOL_SLASH: return "status-protocol-t2";
        case HttpParserState::STATUS_PROTOCOL_VERSION_MAJOR: return "status-protocol-version-major";
        case HttpParserState::STATUS_PROTOCOL_VERSION_MINOR: return "status-protocol-version-minor";
        case HttpParserState::STATUS_CODE_BEGIN: return "status-code-begin";
        case HttpParserState::STATUS_CODE: return "status-code";
        case HttpParserState::STATUS_MESSAGE_BEGIN: return "status-message-begin";
        case HttpParserState::STATUS_MESSAGE: return "status-message";
        case HttpParserState::STATUS_MESSAGE_LF: return "status-message-lf";

        // message header
        case HttpParserState::HEADER_NAME_BEGIN: return "header-name-begin";
        case HttpParserState::HEADER_NAME: return "header-name";
        case HttpParserState::HEADER_COLON: return "header-colon";
        case HttpParserState::HEADER_VALUE_BEGIN: return "header-value-begin";
        case HttpParserState::HEADER_VALUE: return "header-value";
        case HttpParserState::HEADER_VALUE_LF: return "header-value-lf";
        case HttpParserState::HEADER_VALUE_END: return "header-value-end";
        case HttpParserState::HEADER_END_LF: return "header-end-lf";

        // LWS
        case HttpParserState::LWS_BEGIN: return "lws-begin";
        case HttpParserState::LWS_LF: return "lws-lf";
        case HttpParserState::LWS_SP_HT_BEGIN: return "lws-sp-ht-begin";
        case HttpParserState::LWS_SP_HT: return "lws-sp-ht";

        // message content
        case HttpParserState::CONTENT_BEGIN: return "content-begin";
        case HttpParserState::CONTENT: return "content";
        case HttpParserState::CONTENT_ENDLESS: return "content-endless";
        case HttpParserState::CONTENT_CHUNK_SIZE_BEGIN: return "content-chunk-size-begin";
        case HttpParserState::CONTENT_CHUNK_SIZE: return "content-chunk-size";
        case HttpParserState::CONTENT_CHUNK_LF1: return "content-chunk-lf1";
        case HttpParserState::CONTENT_CHUNK_BODY: return "content-chunk-body";
        case HttpParserState::CONTENT_CHUNK_LF2: return "content-chunk-lf2";
        case HttpParserState::CONTENT_CHUNK_CR3: return "content-chunk-cr3";
        case HttpParserState::CONTENT_CHUNK_LF3: return "content-chunk_lf3";
    }

    return "UNKNOWN";
}

} // namespace

bool HttpParser::isProcessingHeader() const noexcept
{
    // XXX should we include request-line and status-line here, too?
    switch (_state)
    {
        case HttpParserState::HEADER_NAME_BEGIN:
        case HttpParserState::HEADER_NAME:
        case HttpParserState::HEADER_COLON:
        case HttpParserState::HEADER_VALUE_BEGIN:
        case HttpParserState::HEADER_VALUE:
        case HttpParserState::HEADER_VALUE_LF:
        case HttpParserState::HEADER_VALUE_END:
        case HttpParserState::HEADER_END_LF: return true;
        default: return false;
    }
}

bool HttpParser::isProcessingBody() const noexcept
{
    switch (_state)
    {
        case HttpParserState::CONTENT_BEGIN:
        case HttpParserState::CONTENT:
        case HttpParserState::CONTENT_ENDLESS:
        case HttpParserState::CONTENT_CHUNK_SIZE_BEGIN:
        case HttpParserState::CONTENT_CHUNK_SIZE:
        case HttpParserState::CONTENT_CHUNK_LF1:
        case HttpParserState::CONTENT_CHUNK_BODY:
        case HttpParserState::CONTENT_CHUNK_LF2:
        case HttpParserState::CONTENT_CHUNK_CR3:
        case HttpParserState::CONTENT_CHUNK_LF3: return true;
        default: return false;
    }
}

HttpParser::HttpParser(HttpParseMode mode, HttpListener* listener) noexcept: _mode(mode), _listener(listener)
{
    assert(listener != nullptr && "listener must not be null");
}

size_t HttpParser::parseFragment(std::string_view chunk) noexcept
{
    /*
     * CR               = 0x0D
     * LF               = 0x0A
     * SP               = 0x20
     * HT               = 0x09
     *
     * CRLF             = CR LF
     * LWS              = [CRLF] 1*( SP | HT )
     *
     * HTTP-message     = Request | Response
     *
     * generic-message  = start-line
     *                    *(message-header CRLF)
     *                    CRLF
     *                    [ message-body ]
     *
     * start-line       = Request-Line | Status-Line
     *
     * Request-Line     = Method SP Request-URI SP HTTP-Version CRLF
     *
     * Method           = "OPTIONS" | "GET" | "HEAD"
     *                  | "POST"    | "PUT" | "DELETE"
     *                  | "TRACE"   | "CONNECT"
     *                  | extension-method
     *
     * Request-URI      = "*" | absoluteURI | abs_path | authority
     *
     * extension-method = token
     *
     * Status-Line      = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
     *
     * HTTP-Version     = "HTTP" "/" 1*DIGIT "." 1*DIGIT
     * Status-Code      = 3*DIGIT
     * Reason-Phrase    = *<TEXT, excluding CR, LF>
     *
     * absoluteURI      = "http://" [user ':' pass '@'] hostname [abs_path] [qury]
     * abs_path         = "/" *CHAR
     * authority        = ...
     * token            = 1*<any CHAR except CTLs or seperators>
     * separator        = "(" | ")" | "<" | ">" | "@"
     *                  | "," | ";" | ":" | "\" | <">
     *                  | "/" | "[" | "]" | "?" | "="
     *                  | "{" | "}" | SP | HT
     *
     * message-header   = field-name ":" [ field-value ]
     * field-name       = token
     * field-value      = *( field-content | LWS )
     * field-content    = <the OCTETs making up the field-value
     *                    and consisting of either *TEXT or combinations
     *                    of token, separators, and quoted-string>
     *
     * message-body     = entity-body
     *                  | <entity-body encoded as per Transfer-Encoding>
     */

    char const* i = chunk.data();
    char const* e = chunk.data() + chunk.size();

    const size_t initialOutOffset = 0;
    size_t result = initialOutOffset;
    size_t* nparsed = &result;

    auto const nextChar = [&](size_t n = 1) {
        i += n;
        *nparsed += n;
        _bytesReceived += n;
    };

#if 0
    switch (_state) {
        case HttpParserState::CONTENT: // fixed size content
            if (!passContent(chunk, nparsed))
                goto done;

            i += *nparsed;
            break;
        case HttpParserState::CONTENT_ENDLESS: // endless-sized content (until stream end)
        {
            *nparsed += chunk.size();
            onMessageContent(chunk);
            goto done;
        }
        default:
            break;
    }
#endif

    while (i != e)
    {
        switch (_state)
        {
            case HttpParserState::MESSAGE_BEGIN:
                _contentLength = -1;
                switch (_mode)
                {
                    case HttpParseMode::REQUEST:
                        _state = HttpParserState::REQUEST_LINE_BEGIN;
                        _versionMajor = 0;
                        _versionMinor = 0;
                        break;
                    case HttpParseMode::RESPONSE:
                        _state = HttpParserState::STATUS_LINE_BEGIN;
                        _code = 0;
                        _versionMajor = 0;
                        _versionMinor = 0;
                        break;
                    case HttpParseMode::MESSAGE:
                        _state = HttpParserState::HEADER_NAME_BEGIN;

                        // an internet message has no special top-line,
                        // so we just invoke the callback right away
                        _listener->onMessageBegin();

                        break;
                }
                break;
            case HttpParserState::REQUEST_LINE_BEGIN:
                if (isToken(*i))
                {
                    _state = HttpParserState::REQUEST_METHOD;
                    _method = chunk.substr(*nparsed - initialOutOffset, 1);
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::REQUEST_METHOD:
                if (*i == SP)
                {
                    _state = HttpParserState::REQUEST_ENTITY_BEGIN;
                    nextChar();
                }
                else if (isToken(*i))
                {
                    _method = std::string_view(_method.data(), _method.size() + 1);
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::REQUEST_ENTITY_BEGIN:
                if (std::isprint(*i))
                {
                    _entity = chunk.substr(*nparsed - initialOutOffset, 1);
                    _state = HttpParserState::REQUEST_ENTITY;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::REQUEST_ENTITY:
                if (*i == SP)
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_BEGIN;
                    nextChar();
                }
                else if (std::isprint(*i))
                {
                    _entity = std::string_view(_entity.data(), _entity.size() + 1);
                    nextChar();
                }
                else if (*i == CR)
                {
                    _state = HttpParserState::REQUEST_0_9_LF;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::REQUEST_0_9_LF:
                if (*i == LF)
                {
                    _listener->onMessageBegin(_method, _entity, HttpVersion::VERSION_0_9);
                    _listener->onMessageHeaderEnd();
                    _listener->onMessageEnd();
                    goto done;
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_BEGIN:
                if (*i != 'H')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_T1;
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_T1:
                if (*i != 'T')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_T2;
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_T2:
                if (*i != 'T')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_P;
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_P:
                if (*i != 'P')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_SLASH;
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_SLASH:
                if (*i != '/')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_VERSION_MAJOR;
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_VERSION_MAJOR:
                if (*i == '.')
                {
                    _state = HttpParserState::REQUEST_PROTOCOL_VERSION_MINOR;
                    nextChar();
                }
                else if (!std::isdigit(*i))
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _versionMajor = _versionMajor * 10 + *i - '0';
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_PROTOCOL_VERSION_MINOR:
                if (*i == CR)
                {
                    _state = HttpParserState::REQUEST_LINE_LF;
                    nextChar();
                }
                else if (!std::isdigit(*i))
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _versionMinor = _versionMinor * 10 + *i - '0';
                    nextChar();
                }
                break;
            case HttpParserState::REQUEST_LINE_LF:
                if (*i == LF)
                {
                    nextChar();
                    auto const httpVersion = makeHttpVersion(_versionMajor, _versionMinor);
                    if (httpVersion != HttpVersion::UNKNOWN)
                    {
                        _state = HttpParserState::HEADER_NAME_BEGIN;
                        _listener->onMessageBegin(_method, _entity, httpVersion);
                    }
                    else
                    {
                        _listener->onProtocolError();
                        _state = HttpParserState::PROTOCOL_ERROR;
                    }
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::STATUS_LINE_BEGIN:
            case HttpParserState::STATUS_PROTOCOL_BEGIN:
                if (*i != 'H')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::STATUS_PROTOCOL_T1;
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_PROTOCOL_T1:
                if (*i != 'T')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::STATUS_PROTOCOL_T2;
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_PROTOCOL_T2:
                if (*i != 'T')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::STATUS_PROTOCOL_P;
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_PROTOCOL_P:
                if (*i != 'P')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::STATUS_PROTOCOL_SLASH;
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_PROTOCOL_SLASH:
                if (*i != '/')
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::STATUS_PROTOCOL_VERSION_MAJOR;
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_PROTOCOL_VERSION_MAJOR:
                if (*i == '.')
                {
                    _state = HttpParserState::STATUS_PROTOCOL_VERSION_MINOR;
                    nextChar();
                }
                else if (!std::isdigit(*i))
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _versionMajor = _versionMajor * 10 + *i - '0';
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_PROTOCOL_VERSION_MINOR:
                if (*i == SP)
                {
                    _state = HttpParserState::STATUS_CODE_BEGIN;
                    nextChar();
                }
                else if (!std::isdigit(*i))
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _versionMinor = _versionMinor * 10 + *i - '0';
                    nextChar();
                }
                break;
            case HttpParserState::STATUS_CODE_BEGIN:
                if (!std::isdigit(*i))
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                    break;
                }
                _state = HttpParserState::STATUS_CODE;
            /* fall through */
            case HttpParserState::STATUS_CODE:
                if (std::isdigit(*i))
                {
                    _code = _code * 10 + *i - '0';
                    nextChar();
                }
                else if (*i == SP)
                {
                    _state = HttpParserState::STATUS_MESSAGE_BEGIN;
                    nextChar();
                }
                else if (*i == CR)
                { // no Status-Message passed
                    _state = HttpParserState::STATUS_MESSAGE_LF;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::STATUS_MESSAGE_BEGIN:
                if (isText(*i))
                {
                    _state = HttpParserState::STATUS_MESSAGE;
                    _message = chunk.substr(*nparsed - initialOutOffset, 1);
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::STATUS_MESSAGE:
                if (isText(*i) && *i != CR && *i != LF)
                {
                    _message = std::string_view(_message.data(), _message.size() + 1);
                    nextChar();
                }
                else if (*i == CR)
                {
                    _state = HttpParserState::STATUS_MESSAGE_LF;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::STATUS_MESSAGE_LF:
                if (*i == LF)
                {
                    nextChar();
                    auto const httpVersion = makeHttpVersion(_versionMajor, _versionMinor);
                    // TODO: check for valid status code
                    if (httpVersion != HttpVersion::UNKNOWN)
                    {
                        _state = HttpParserState::HEADER_NAME_BEGIN;
                        _listener->onMessageBegin(httpVersion, static_cast<HttpStatus>(_code), _message);
                    }
                    else
                    {
                        _listener->onProtocolError();
                        _state = HttpParserState::PROTOCOL_ERROR;
                    }
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::HEADER_NAME_BEGIN:
                if (isToken(*i))
                {
                    _name = chunk.substr(*nparsed - initialOutOffset, 1);
                    _state = HttpParserState::HEADER_NAME;
                    nextChar();
                }
                else if (*i == CR)
                {
                    _state = HttpParserState::HEADER_END_LF;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::HEADER_NAME:
                if (isToken(*i))
                {
                    _name = std::string_view(_name.data(), _name.size() + 1);
                    nextChar();
                }
                else if (*i == ':')
                {
                    _state = HttpParserState::LWS_BEGIN;
                    _lwsNext = HttpParserState::HEADER_VALUE_BEGIN;
                    _lwsNull = HttpParserState::HEADER_VALUE_END; // only (CR LF) parsed, assume empty
                                                                  // value & go on with next header
                    nextChar();
                }
                else if (*i == CR)
                {
                    _state = HttpParserState::LWS_LF;
                    _lwsNext = HttpParserState::HEADER_COLON;
                    _lwsNull = HttpParserState::PROTOCOL_ERROR;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::HEADER_COLON:
                if (*i == ':')
                {
                    _state = HttpParserState::LWS_BEGIN;
                    _lwsNext = HttpParserState::HEADER_VALUE_BEGIN;
                    _lwsNull = HttpParserState::HEADER_VALUE_END;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::LWS_BEGIN:
                if (*i == CR)
                {
                    _state = HttpParserState::LWS_LF;
                    nextChar();
                }
                else if (*i == SP || *i == HT)
                {
                    _state = HttpParserState::LWS_SP_HT;
                    nextChar();
                }
                else if (std::isprint(*i))
                {
                    _state = _lwsNext;
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::LWS_LF:
                if (*i == LF)
                {
                    _state = HttpParserState::LWS_SP_HT_BEGIN;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::LWS_SP_HT_BEGIN:
                if (*i == SP || *i == HT)
                {
                    if (!_value.empty())
                        _value = std::string_view(_value.data(), _value.size() + 3); // CR LF (SP | HT)

                    _state = HttpParserState::LWS_SP_HT;
                    nextChar();
                }
                else
                {
                    // only (CF LF) parsed so far and no 1*(SP | HT) found.
                    if (_lwsNull == HttpParserState::PROTOCOL_ERROR)
                    {
                        _listener->onProtocolError();
                    }
                    _state = _lwsNull;
                    // XXX no nparsed/i-update
                }
                break;
            case HttpParserState::LWS_SP_HT:
                if (*i == SP || *i == HT)
                {
                    if (!_value.empty())
                        _value = std::string_view(_value.data(), _value.size() + 1); // (SP | HT)

                    nextChar();
                }
                else
                    _state = _lwsNext;
                break;
            case HttpParserState::HEADER_VALUE_BEGIN:
                if (isText(*i))
                {
                    _value = chunk.substr(*nparsed - initialOutOffset, 1);
                    nextChar();
                    _state = HttpParserState::HEADER_VALUE;
                }
                else if (*i == CR)
                {
                    _state = HttpParserState::HEADER_VALUE_LF;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::HEADER_VALUE:
                if (*i == CR)
                {
                    _state = HttpParserState::LWS_LF;
                    _lwsNext = HttpParserState::HEADER_VALUE;
                    _lwsNull = HttpParserState::HEADER_VALUE_END;
                    nextChar();
                }
                else if (isText(*i))
                {
                    _value = std::string_view(_value.data(), _value.size() + 1);
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::HEADER_VALUE_LF:
                if (*i == LF)
                {
                    _state = HttpParserState::HEADER_VALUE_END;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::HEADER_VALUE_END: {
                if (iequals(_name, "Content-Length"))
                {
                    _contentLength = parseInt(_value);
                    // do not pass header to upper layer
                    // as this is an HTTP/1 transport-layer specific header
                    _listener->onMessageHeader(_name, _value);
                    // XXX well, maybe nevertheless
                }
                else if (iequals(_name, "Transfer-Encoding"))
                {
                    if (iequals(_value, "chunked"))
                    {
                        _chunked = true;
                        // do not pass header to upper layer
                        // as this is an HTTP/1 transport-layer specific header
                    }
                    else
                    {
                        _listener->onMessageHeader(_name, _value);
                    }
                }
                else
                {
                    _listener->onMessageHeader(_name, _value);
                }

                _name = {};
                _value = {};

                // continue with the next header
                _state = HttpParserState::HEADER_NAME_BEGIN;

                break;
            }
            case HttpParserState::HEADER_END_LF:
                if (*i == LF)
                {
                    if (isContentExpected())
                        _state = HttpParserState::CONTENT_BEGIN;
                    else
                        _state = HttpParserState::MESSAGE_BEGIN;

                    nextChar();

                    _listener->onMessageHeaderEnd();

                    if (!isContentExpected())
                    {
                        _listener->onMessageEnd();
                        goto done;
                    }
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::CONTENT_BEGIN:
                if (_chunked)
                    _state = HttpParserState::CONTENT_CHUNK_SIZE_BEGIN;
                else if (_contentLength >= 0)
                    _state = HttpParserState::CONTENT;
                else
                    _state = HttpParserState::CONTENT_ENDLESS;
                break;
            case HttpParserState::CONTENT_ENDLESS: {
                // body w/o content-length (allowed in simple MESSAGE types only)
                auto const c = chunk.substr(*nparsed - initialOutOffset);
                nextChar(c.size());
                _listener->onMessageContent(c);
                break;
            }
            case HttpParserState::CONTENT: {
                // fixed size content length
                std::size_t offset = *nparsed - initialOutOffset;
                std::size_t chunkSize = std::min(static_cast<size_t>(_contentLength), chunk.size() - offset);

                _contentLength -= chunkSize;
                nextChar(chunkSize);

                _listener->onMessageContent(chunk.substr(offset, chunkSize));

                if (_contentLength == 0)
                    _state = HttpParserState::MESSAGE_BEGIN;

                if (_state == HttpParserState::MESSAGE_BEGIN)
                {
                    _listener->onMessageEnd();
                    goto done;
                }

                break;
            }
            case HttpParserState::CONTENT_CHUNK_SIZE_BEGIN:
                if (!std::isxdigit(*i))
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                    break;
                }
                _state = HttpParserState::CONTENT_CHUNK_SIZE;
                _contentLength = 0;
            /* fall through */
            case HttpParserState::CONTENT_CHUNK_SIZE:
                if (*i == CR)
                {
                    _state = HttpParserState::CONTENT_CHUNK_LF1;
                    nextChar();
                }
                else if (*i >= '0' && *i <= '9')
                {
                    _contentLength = _contentLength * 16 + *i - '0';
                    nextChar();
                }
                else if (*i >= 'a' && *i <= 'f')
                {
                    _contentLength = _contentLength * 16 + 10 + *i - 'a';
                    nextChar();
                }
                else if (*i >= 'A' && *i <= 'F')
                {
                    _contentLength = _contentLength * 16 + 10 + *i - 'A';
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::CONTENT_CHUNK_LF1:
                if (*i != LF)
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    if (_contentLength != 0)
                        _state = HttpParserState::CONTENT_CHUNK_BODY;
                    else
                        _state = HttpParserState::CONTENT_CHUNK_CR3;

                    nextChar();
                }
                break;
            case HttpParserState::CONTENT_CHUNK_BODY:
                if (_contentLength)
                {
                    std::size_t offset = *nparsed - initialOutOffset;
                    std::size_t chunkSize =
                        std::min(static_cast<size_t>(_contentLength), chunk.size() - offset);
                    _contentLength -= chunkSize;
                    nextChar(chunkSize);

                    _listener->onMessageContent(chunk.substr(offset, chunkSize));
                }
                else if (*i == CR)
                {
                    _state = HttpParserState::CONTENT_CHUNK_LF2;
                    nextChar();
                }
                else
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                break;
            case HttpParserState::CONTENT_CHUNK_LF2:
                if (*i != LF)
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::CONTENT_CHUNK_SIZE;
                    nextChar();
                }
                break;
            case HttpParserState::CONTENT_CHUNK_CR3:
                if (*i != CR)
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    _state = HttpParserState::CONTENT_CHUNK_LF3;
                    nextChar();
                }
                break;
            case HttpParserState::CONTENT_CHUNK_LF3:
                if (*i != LF)
                {
                    _listener->onProtocolError();
                    _state = HttpParserState::PROTOCOL_ERROR;
                }
                else
                {
                    nextChar();

                    _state = HttpParserState::MESSAGE_BEGIN;

                    _listener->onMessageEnd();
                    goto done;
                }
                break;
            case HttpParserState::PROTOCOL_ERROR: goto done;
            default: goto done;
        }
    }
    // we've reached the end of the chunk

    if (_state == HttpParserState::CONTENT_BEGIN)
    {
        // we've just parsed all headers but no body yet.

        if (_contentLength < 0 && !_chunked && _mode != HttpParseMode::MESSAGE)
        {
            // and there's no body to come

            // subsequent calls to process() parse next request(s).
            _state = HttpParserState::MESSAGE_BEGIN;

            _listener->onMessageEnd();
            goto done;
        }
    }

done:
    return *nparsed - initialOutOffset;
}

void HttpParser::reset() noexcept
{
    _state = HttpParserState::MESSAGE_BEGIN;
    _bytesReceived = 0;
}
