CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -O2 -pthread
LDFLAGS  := -pthread
COVFLAGS  := -fprofile-instr-generate -fcoverage-mapping
CLANG_TIDY   := /opt/homebrew/opt/llvm/bin/clang-tidy
CLANG_FORMAT := /opt/homebrew/opt/llvm/bin/clang-format
LINT_SRCS    := src/resp.cpp src/store.cpp src/commands.cpp src/server.cpp
FMT_SRCS     := $(wildcard src/*.cpp src/*.h)

TARGET    := redis-server
SRCS      := src/resp.cpp src/store.cpp src/commands.cpp src/server.cpp src/main.cpp
OBJS      := $(SRCS:.cpp=.o)
TEST_BINS := tests/test_store tests/test_resp

.PHONY: all clean run test coverage coverage-html lint fmt fmt-check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

tests/test_store: tests/test_store.cpp src/store.o
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

tests/test_resp: tests/test_resp.cpp src/resp.o
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(TEST_BINS)
	@./tests/test_store
	@./tests/test_resp

# ── Coverage ──────────────────────────────────────────────────────────────────
tests/test_store_cov: tests/test_store.cpp src/store.cpp
	$(CXX) $(CXXFLAGS) $(COVFLAGS) $^ $(LDFLAGS) -o $@

tests/test_resp_cov: tests/test_resp.cpp src/resp.cpp
	$(CXX) $(CXXFLAGS) $(COVFLAGS) $^ -o $@

coverage: tests/test_store_cov tests/test_resp_cov
	@LLVM_PROFILE_FILE="tests/store.profraw" ./tests/test_store_cov > /dev/null
	@LLVM_PROFILE_FILE="tests/resp.profraw"  ./tests/test_resp_cov  > /dev/null
	@xcrun llvm-profdata merge -sparse tests/store.profraw -o tests/store.profdata
	@xcrun llvm-profdata merge -sparse tests/resp.profraw  -o tests/resp.profdata
	@echo "\n── store.cpp ────────────────────────────────────────────────────────"
	@xcrun llvm-cov report tests/test_store_cov -instr-profile=tests/store.profdata src/store.cpp
	@echo "\n── resp.cpp ─────────────────────────────────────────────────────────"
	@xcrun llvm-cov report tests/test_resp_cov  -instr-profile=tests/resp.profdata  src/resp.cpp

coverage-html: tests/test_store_cov tests/test_resp_cov
	@LLVM_PROFILE_FILE="tests/store.profraw" ./tests/test_store_cov > /dev/null
	@LLVM_PROFILE_FILE="tests/resp.profraw"  ./tests/test_resp_cov  > /dev/null
	@xcrun llvm-profdata merge -sparse tests/store.profraw tests/resp.profraw -o tests/merged.profdata
	@xcrun llvm-cov show \
		tests/test_store_cov \
		-object=tests/test_resp_cov \
		-format=html \
		-instr-profile=tests/merged.profdata \
		-output-dir=tests/coverage-html \
		src/store.cpp src/resp.cpp
	@open tests/coverage-html/index.html

# ── Format ────────────────────────────────────────────────────────────────────
fmt:
	@$(CLANG_FORMAT) -i $(FMT_SRCS)

fmt-check:
	@$(CLANG_FORMAT) --dry-run --Werror $(FMT_SRCS)

# ── Lint ──────────────────────────────────────────────────────────────────────
lint:
	@$(CLANG_TIDY) $(LINT_SRCS) -- $(CXXFLAGS)

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BINS) \
	      tests/test_store_cov tests/test_resp_cov \
	      tests/*.profraw tests/*.profdata
