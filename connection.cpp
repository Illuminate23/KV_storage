#include "connection.h"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <span>
#include <unistd.h>

#include "common.h"
#include "protocol.h"
#include "server.h"


bool Connection::serveOne(Server &server) {
    // заголовок кадра: 4-байтная длина (little-endian)
    if (inbox.size() < 4) {
        return false;       // нужно больше данных
    }
    uint32_t bodyLen = 0;
    std::memcpy(&bodyLen, inbox.data(), 4);
    if (bodyLen > kMaxMessage) {
        logLine("frame too long");
        wantClose = true;
        return false;
    }
    // тело кадра
    if (4 + bodyLen > inbox.size()) {
        return false;       // нужно больше данных
    }
    std::span<const uint8_t> body{inbox.data() + 4, bodyLen};

    std::vector<std::string> args;
    if (!parseRequest(body, args)) {
        logLine("malformed request");
        wantClose = true;
        return false;
    }

    // резервируем заголовок длины ответа, выполняем команду, затем проставляем длину
    size_t header = outbox.size();
    outbox.appendU32(0);
    {
        Response reply(outbox);
        server.dispatch(args, reply);
    }
    size_t replyLen = outbox.size() - header - 4;
    if (replyLen > kMaxMessage) {
        outbox.resize(header + 4);
        Response reply(outbox);
        reply.writeError(ErrCode::TooBig, "response is too big.");
        replyLen = outbox.size() - header - 4;
    }
    uint32_t lenField = static_cast<uint32_t>(replyLen);
    std::memcpy(outbox.data() + header, &lenField, 4);

    // выбрасываем только что обслуженный запрос; за ним может быть ещё (конвейер)
    inbox.consume(4 + bodyLen);
    return true;
}

void Connection::handleWritable() {
    assert(outbox.size() > 0);
    ssize_t sent = write(fd_, outbox.data(), outbox.size());
    if (sent < 0 && errno == EAGAIN) {
        return;             // сокет на самом деле не готов
    }
    if (sent < 0) {
        logErrno("write() error");
        wantClose = true;
        return;
    }
    outbox.consume(static_cast<size_t>(sent));
    if (outbox.size() == 0) {       // всё отправлено
        wantRead = true;
        wantWrite = false;
    }
}

void Connection::handleReadable(Server &server) {
    std::array<uint8_t, 64 * 1024> chunk;
    ssize_t got = read(fd_, chunk.data(), chunk.size());
    if (got < 0 && errno == EAGAIN) {
        return;             // сокет на самом деле не готов
    }
    if (got < 0) {
        logErrno("read() error");
        wantClose = true;
        return;
    }
    if (got == 0) {         // соединение закрыто другой стороной
        logLine(inbox.size() == 0 ? "client closed" : "unexpected EOF");
        wantClose = true;
        return;
    }
    inbox.append(chunk.data(), static_cast<size_t>(got));
    while (serveOne(server)) {}     // разбираем все накопленные запросы

    if (outbox.size() > 0) {        // ответ сформирован
        wantRead = false;
        wantWrite = true;
        handleWritable();           // пробуем отправить сразу, не дожидаясь poll()
    }
}
