#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol.h"

class Database;


// Основа паттерна «Команда»: каждая команда протокола — это объект, который
// знает своё имя, точное число токенов и как выполниться над базой.
class Command {
public:
    virtual ~Command() = default;

    virtual const char *verb() const = 0;
    // точное число токенов, включая само имя команды
    virtual size_t arity() const = 0;

    virtual void run(std::vector<std::string> &args,
                     Response &reply, Database &db) = 0;
};

// Хранит по одному экземпляру каждой команды и направляет запрос к нужной по имени.
class CommandTable {
public:
    CommandTable();

    void run(std::vector<std::string> &args, Response &reply, Database &db);

private:
    void enroll(std::unique_ptr<Command> cmd);

    std::unordered_map<std::string, std::unique_ptr<Command>> byVerb_;
};
