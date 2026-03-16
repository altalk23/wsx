#include <wsx/Client.hpp>
#include "UrlParser.hpp"
#include "SyncTransports.hpp"

#include <qsox/Resolver.hpp>

namespace wsx {

Result<Client> Client::connect(std::string_view url) {
    GEODE_UNWRAP_INTO(auto parts, wsx::parseUrl(url));

    auto addr = qsox::SocketAddress::any();
    addr.setPort(parts.port);

    // resolve hostname if needed
    if (parts.ip) {
        addr.setAddress(*parts.ip);
    } else {
        if (parts.hostname.empty()) {
            return Err("URL must contain a hostname or IP address");
        }
        auto result = qsox::resolver::resolve(std::string{parts.hostname});
        if (!result) {
            return Err(fmt::format("Could not resolve host '{}': {}", parts.hostname, result.unwrapErr()));
        }

        addr.setAddress(result.unwrap());
    }

    ClientConnectOptions opts {
        .path = parts.path,
        .hostname = parts.hostname,
        .address = addr,
    };

    if (parts.tls) {
#ifdef WSX_ENABLE_TLS
        GEODE_UNWRAP_INTO(opts.tlsContext, createContext());
#else
        return Err("wsx was not built with TLS support, cannot connect to wss:// URLs");
#endif
    }

    return connect(opts);
}

Result<Client> Client::connect(const ClientConnectOptions& options) {
    std::unique_ptr<WsTransport> transport;
#ifdef WSX_ENABLE_TLS
    if (options.tlsContext) {
        GEODE_UNWRAP_INTO(transport, TlsTransport::create(options.address, options.tlsContext, options.hostname));
    }
#endif
    if (!transport) {
        GEODE_UNWRAP_INTO(transport, TcpTransport::create(options.address));
    }
    return doHandshake(std::move(transport), options);
}

Result<Client> Client::doHandshake(std::unique_ptr<WsTransport> transport, const ClientConnectOptions& options) {
    uint8_t nonce[16];
    auto req = generateRequest(nonce, options);
    GEODE_UNWRAP(transport->send(req.data(), req.size()));

    Client client(std::move(transport));
    size_t httpSize = 0;

    while (true) {
        auto wnd = client.rwindow(4096);
        size_t recvd = GEODE_UNWRAP(client.m_transport->receive(wnd.data(), wnd.size()));
        client.m_rbuf.advanceWrite(recvd);

        auto wrpread = client.m_rbuf.peek(client.m_rbuf.size());

        std::string_view view{(const char*)wrpread.first.data(), wrpread.size()};

        // stop once we have received the full HTTP response headers (indicated by \r\n\r\n)
        auto pos = view.find("\r\n\r\n");
        if (pos != std::string_view::npos) {
            httpSize = pos + 4;
            break;
        }
    }

    std::string httpResponse(httpSize, '\0');
    client.m_rbuf.read(httpResponse.data(), httpSize);
    GEODE_UNWRAP(parseResponse(nonce, httpResponse));

    return Ok(std::move(client));
}

Result<void> Client::ping(std::span<const uint8_t> payload) {
    return send(Message(Message::Type::Ping, std::vector(payload.begin(), payload.end())));
}

Result<void> Client::send(Message&& message) {
    if (!m_transport) return Err("Connection is closed");

    GEODE_UNWRAP(write(std::move(message)));
    return flush();
}

Result<void> Client::send(std::string_view text) {
    return send(Message(text));
}

Result<void> Client::send(std::span<const uint8_t> binary) {
    return send(Message(binary));
}

Result<void> Client::write(Message&& message) {
    if (!m_transport) return Err("Connection is closed");

    return _writeMessage(m_wbuf, message);
}

Result<void> Client::write(std::string_view text) {
    return write(Message(text));
}

Result<void> Client::write(std::span<const uint8_t> binary) {
    return write(Message(binary));
}

Result<void> Client::flush() {
    if (!m_transport) return Err("Connection is closed");

    while (!m_wbuf.empty()) {
        auto wrp = m_wbuf.peek(m_wbuf.size());
        GEODE_UNWRAP_INTO(auto sent, m_transport->send(wrp.first.data(), wrp.first.size()));
        m_wbuf.skip(sent);
    }
    return Ok();
}

Result<Message> Client::recv() {
    if (!m_transport) return Err("Connection is closed");

    while (true) {
        auto res = readFromBuffer();
        if (!res) {
            m_transport.reset();
            return Err(fmt::format("decoding failed: {}", std::move(res).unwrapErr()));
        }

        auto opt = std::move(res).unwrap();
        if (opt) return Ok(std::move(*opt));

        // read from the socket
        auto wnd = this->rwindow(4096);
        size_t recvd = GEODE_UNWRAP(m_transport->receive(wnd.data(), wnd.size()));
        m_rbuf.advanceWrite(recvd);
    }
}

Result<> Client::close(uint16_t code, std::string_view reason) {
    GEODE_UNWRAP(sendCloseFrame(code, reason));

    // wait for a response close frame
    while (true) {
        auto res = this->recv();
        if (!res) {
            m_transport.reset();
            return Err(std::move(res).unwrapErr());
        }

        if (res.unwrap().isClose()) {
            break;
        }
    }

    m_transport.reset();
    return Ok();
}

Result<> Client::closeNoAck(uint16_t code, std::string_view reason) {
    GEODE_UNWRAP(this->sendCloseFrame(code, reason));
    m_transport.reset();
    return Ok();
}

Result<> Client::sendCloseFrame(uint16_t code, std::string_view reason) {
    if (!m_transport) return Err("Connection is already closed");
    auto res = this->send(makeCloseFrame(code, reason));
    if (!res) {
        m_transport.reset();
        return Err(fmt::format("failed to send close frame: {}", res.unwrapErr()));
    }
    return Ok();
}

}
