#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "list.h"
#include "thread_pool.h"
#include "database.h"
#include "command.h"

class Connection;


// Сетевой фронт: владеет слушающим сокетом, активными соединениями, базой,
// пулом потоков и таблицей команд. Запускает один цикл событий на poll() и
// прокачивает ввод-вывод всех соединений в одном потоке.
class Server {
public:
    explicit Server(uint16_t port);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    void run();

    // выполнить один разобранный запрос, записав ответ в буфер
    void dispatch(std::vector<std::string> &args, Response &reply) {
        commands_.run(args, reply, db_);
    }

private:
    int listenFd_ = -1;

    // соединения, индексируемые по файловому дескриптору
    std::vector<std::unique_ptr<Connection>> connections_;
    // страж списка таймаутов простоя
    LinkedNode idleList_;

    // pool_ объявлен раньше db_: пул должен пережить использующую его базу
    ThreadPool pool_;
    Database db_;
    CommandTable commands_;

    void accept();
    void drop(Connection *conn);

    int32_t nextTimeoutMs();
    void fireTimers();

    static void makeNonBlocking(int fd);
};
