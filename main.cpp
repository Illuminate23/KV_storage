#include "server.h"


// Точка входа: создаём сервер на порту 1234 и запускаем цикл событий.
int main() {
    Server server(1234);
    server.run();
    return 0;
}
