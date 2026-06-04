#include "command.h"

#include <utility>

#include "common.h"
#include "database.h"
#include "entry.h"
#include "value.h"
#include "zset.h"


// Получить по ключу его сортированное множество:
//   - отсутствующий ключ ведёт себя как общий пустой набор,
//   - ключ другого типа даёт null (ошибка типа).
static SortedSet *resolveSet(Database &db, const std::string &key) {
    Record *record = db.find(key);
    if (!record) {
        return SortedSet::blank();
    }
    if (record->value->kind() != ValueKind::SortedSet) {
        return nullptr;
    }
    return &record->asSortedSet()->set;
}

// --- строковые команды ----------------------------------------------------

// GET key
class GetCommand final : public Command {
public:
    const char *verb() const override { return "get"; }
    size_t arity() const override { return 2; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        Record *record = db.find(args[1]);
        if (!record) {
            return reply.writeNil();
        }
        if (record->value->kind() != ValueKind::String) {
            return reply.writeError(ErrCode::BadType, "not a string value");
        }
        reply.writeString(record->asString()->bytes);
    }
};

// SET key value
class SetCommand final : public Command {
public:
    const char *verb() const override { return "set"; }
    size_t arity() const override { return 3; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        Record *record = db.find(args[1]);
        if (record) {
            if (record->value->kind() != ValueKind::String) {
                return reply.writeError(ErrCode::BadType, "a non-string value exists");
            }
            record->asString()->bytes = std::move(args[2]);
        } else {
            record = Record::makeString();
            record->key = std::move(args[1]);
            record->asString()->bytes = std::move(args[2]);
            db.insert(record);
        }
        reply.writeNil();
    }
};

// DEL key
class DelCommand final : public Command {
public:
    const char *verb() const override { return "del"; }
    size_t arity() const override { return 2; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        Record *record = db.detach(args[1]);
        if (record) {
            db.dispose(record);
        }
        reply.writeInt(record ? 1 : 0);
    }
};

// KEYS
class KeysCommand final : public Command {
public:
    const char *verb() const override { return "keys"; }
    size_t arity() const override { return 1; }
    void run(std::vector<std::string> &, Response &reply, Database &db) override {
        reply.writeArray((uint32_t)db.size());
        db.forEach([&](Record *record) {
            reply.writeString(record->key.data(), record->key.size());
            return true;
        });
    }
};

// --- команды TTL ----------------------------------------------------------

// PEXPIRE key ttl_ms
class ExpireCommand final : public Command {
public:
    const char *verb() const override { return "pexpire"; }
    size_t arity() const override { return 3; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        int64_t ttlMs = 0;
        if (!parseInt(args[2], ttlMs)) {
            return reply.writeError(ErrCode::BadArg, "expect int64");
        }
        Record *record = db.find(args[1]);
        if (record) {
            db.setExpiry(record, ttlMs);
        }
        reply.writeInt(record ? 1 : 0);
    }
};

// PTTL key
class TtlCommand final : public Command {
public:
    const char *verb() const override { return "pttl"; }
    size_t arity() const override { return 2; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        Record *record = db.find(args[1]);
        if (!record) {
            return reply.writeInt(-2);      // ключа нет
        }
        if (record->timerIndex == kNoTimer) {
            return reply.writeInt(-1);      // у ключа нет TTL
        }
        uint64_t deadline = db.timers()[record->timerIndex].deadline;
        uint64_t now = nowMillis();
        reply.writeInt(deadline > now ? (int64_t)(deadline - now) : 0);
    }
};

// --- команды сортированного множества -------------------------------------

// ZADD zset score name
class ZAddCommand final : public Command {
public:
    const char *verb() const override { return "zadd"; }
    size_t arity() const override { return 4; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        double score = 0;
        if (!parseDouble(args[2], score)) {
            return reply.writeError(ErrCode::BadArg, "expect float");
        }
        Record *record = db.find(args[1]);
        SortedSet *set = nullptr;
        if (!record) {
            record = Record::makeSortedSet();
            record->key = std::move(args[1]);
            set = &record->asSortedSet()->set;
            db.insert(record);
        } else {
            if (record->value->kind() != ValueKind::SortedSet) {
                return reply.writeError(ErrCode::BadType, "expect zset");
            }
            set = &record->asSortedSet()->set;
        }
        const std::string &member = args[3];
        bool added = set->add(member.data(), member.size(), score);
        reply.writeInt((int64_t)added);
    }
};

// ZREM zset name
class ZRemCommand final : public Command {
public:
    const char *verb() const override { return "zrem"; }
    size_t arity() const override { return 3; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        SortedSet *set = resolveSet(db, args[1]);
        if (!set) {
            return reply.writeError(ErrCode::BadType, "expect zset");
        }
        const std::string &member = args[2];
        SortedNode *node = set->find(member.data(), member.size());
        if (node) {
            set->erase(node);
        }
        reply.writeInt(node ? 1 : 0);
    }
};

// ZSCORE zset name
class ZScoreCommand final : public Command {
public:
    const char *verb() const override { return "zscore"; }
    size_t arity() const override { return 3; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        SortedSet *set = resolveSet(db, args[1]);
        if (!set) {
            return reply.writeError(ErrCode::BadType, "expect zset");
        }
        const std::string &member = args[2];
        SortedNode *node = set->find(member.data(), member.size());
        if (node) {
            reply.writeDouble(node->score);
        } else {
            reply.writeNil();
        }
    }
};

// ZQUERY zset score name offset limit
class ZQueryCommand final : public Command {
public:
    const char *verb() const override { return "zquery"; }
    size_t arity() const override { return 6; }
    void run(std::vector<std::string> &args, Response &reply, Database &db) override {
        double score = 0;
        if (!parseDouble(args[2], score)) {
            return reply.writeError(ErrCode::BadArg, "expect fp number");
        }
        const std::string &member = args[3];
        int64_t offset = 0, limit = 0;
        if (!parseInt(args[4], offset) || !parseInt(args[5], limit)) {
            return reply.writeError(ErrCode::BadArg, "expect int");
        }
        SortedSet *set = resolveSet(db, args[1]);
        if (!set) {
            return reply.writeError(ErrCode::BadType, "expect zset");
        }
        if (limit <= 0) {
            return reply.writeArray(0);
        }
        SortedNode *node = set->lowerBound(score, member.data(), member.size());
        node = SortedSet::step(node, offset);

        size_t mark = reply.beginArray();
        int64_t produced = 0;
        while (node && produced < limit) {
            reply.writeString(node->name, node->nameLen);
            reply.writeDouble(node->score);
            node = SortedSet::step(node, +1);
            produced += 2;
        }
        reply.endArray(mark, (uint32_t)produced);
    }
};

// --- таблица команд -------------------------------------------------------

void CommandTable::enroll(std::unique_ptr<Command> cmd) {
    byVerb_[cmd->verb()] = std::move(cmd);
}

CommandTable::CommandTable() {
    enroll(std::make_unique<GetCommand>());
    enroll(std::make_unique<SetCommand>());
    enroll(std::make_unique<DelCommand>());
    enroll(std::make_unique<KeysCommand>());
    enroll(std::make_unique<ExpireCommand>());
    enroll(std::make_unique<TtlCommand>());
    enroll(std::make_unique<ZAddCommand>());
    enroll(std::make_unique<ZRemCommand>());
    enroll(std::make_unique<ZScoreCommand>());
    enroll(std::make_unique<ZQueryCommand>());
}

void CommandTable::run(std::vector<std::string> &args, Response &reply, Database &db) {
    if (args.empty()) {
        return reply.writeError(ErrCode::Unknown, "unknown command.");
    }
    auto it = byVerb_.find(args[0]);
    if (it == byVerb_.end() || args.size() != it->second->arity()) {
        // неизвестное имя или неверное число токенов — ответ как в оригинале
        return reply.writeError(ErrCode::Unknown, "unknown command.");
    }
    it->second->run(args, reply, db);
}
