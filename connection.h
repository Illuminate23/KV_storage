#pragma once

#include <cstdint>

#include "buffer.h"
#include "list.h"

class Server;


// Одно клиентское соединение: его сокет, буферизованный ввод-вывод, желаемая
// готовность для цикла событий и место в списке таймаутов простоя. Server
// прокачивает его через handleReadable()/handleWritable().
class Connection {
public:
    explicit Connection(int fd) : fd_(fd) {}

    int fd() const { return fd_; }

    // что соединение сейчас хочет от poll()
    bool wantRead = false;
    bool wantWrite = false;
    bool wantClose = false;

    // буферизованный ввод-вывод
    Buffer inbox;       // принятые байты, ожидающие разбора
    Buffer outbox;      // подготовленные байты, ожидающие отправки

    // учёт таймаута простоя
    uint64_t lastActiveMs = 0;
    LinkedNode idleLink;

    void handleReadable(Server &server);
    void handleWritable();

private:
    int fd_;

    // попытаться обслужить один полностью накопленный запрос; false означает
    // «нужно больше данных» (или соединение нужно закрыть)
    bool serveOne(Server &server);
};
