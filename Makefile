CXX      := g++
# -Wno-invalid-offsetof: offsetof() по встроенным (non-standard-layout) узлам
# здесь намеренный и корректный для данного компилятора приём.
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Wno-invalid-offsetof
LDFLAGS  := -pthread

SRCS := main.cpp server.cpp connection.cpp database.cpp command.cpp \
        protocol.cpp zset.cpp avl.cpp hashtable.cpp heap.cpp \
        thread_pool.cpp common.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

TARGET := server

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# компиляция + генерация файлов зависимостей по заголовкам
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
