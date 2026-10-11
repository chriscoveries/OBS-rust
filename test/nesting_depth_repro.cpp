/*
obs-websocket — issue #99 reproducer: deeply nested JSON/MsgPack payloads.

Build (x86_64 Linux):
  g++ -fsanitize=address,undefined -std=c++17 -g -I../src \
      -I/path/to/nlohmann nesting_depth_repro.cpp -o nesting_depth_repro

Modes:
  scan                 — the depth gates accept <=512 and reject >512.
  parse-json <depth>   — json::parse on a document nested <depth> deep,
                         bypassing the gate: master's behaviour. Deep
                         payloads stack-overflow the recursive parser.
  parse-msgpack <depth> — same through json::from_msgpack.
*/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "websocketserver/NestingDepth.h"

using json = nlohmann::json;

static std::string deepJson(size_t depth)
{
	return std::string(depth, '[') + std::string(depth, ']');
}

/* One-level-deeper fixarray wrapper: 0x91 (array of 1). */
static std::string deepMsgpack(size_t depth)
{
	std::string s(depth, (char)0x91);
	s += (char)0xc0; // nil leaf
	return s;
}

static int scan()
{
	int fails = 0;
	auto check = [&](bool cond, const char *what) {
		if (!cond) {
			fprintf(stderr, "FAIL %s\n", what);
			fails++;
		}
	};

	check(jsonNestingWithinLimit(deepJson(MAX_PAYLOAD_NESTING_DEPTH)), "json at limit");
	check(!jsonNestingWithinLimit(deepJson(MAX_PAYLOAD_NESTING_DEPTH + 1)), "json over limit");
	check(jsonNestingWithinLimit("[{\"a\":[1,{\"b\":null}]}]"), "json normal doc");
	check(!jsonNestingWithinLimit(deepJson(100000)), "json deep doc");

	check(msgpackNestingWithinLimit(deepMsgpack(MAX_PAYLOAD_NESTING_DEPTH)), "msgpack at limit");
	check(!msgpackNestingWithinLimit(deepMsgpack(MAX_PAYLOAD_NESTING_DEPTH + 1)), "msgpack over limit");
	check(!msgpackNestingWithinLimit(deepMsgpack(100000)), "msgpack deep doc");

	printf(fails ? "scan: %d FAILURES\n" : "scan: all pass\n", fails);
	return fails;
}

int main(int argc, char **argv)
{
	if (argc < 2 || !strcmp(argv[1], "scan"))
		return scan();

	size_t depth = argc > 2 ? strtoul(argv[2], nullptr, 10) : 100000;
	if (!strcmp(argv[1], "parse-json")) {
		volatile size_t n = json::parse(deepJson(depth)).size();
		(void)n;
	} else if (!strcmp(argv[1], "parse-msgpack")) {
		volatile size_t n = json::from_msgpack(deepMsgpack(depth)).size();
		(void)n;
	} else {
		fprintf(stderr, "unknown mode\n");
		return 2;
	}
	printf("parsed depth %zu without crashing\n", depth);
	return 0;
}
