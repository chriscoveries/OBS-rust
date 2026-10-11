/*
obs-websocket
Copyright (C) 2016-2021 Stephane Lepin <stephane.lepin@gmail.com>
Copyright (C) 2020-2021 Kyle Manning <tt2468@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* nlohmann::json parses JSON and MsgPack containers by recursive descent,
   one stack frame per nesting level, so a deeply nested payload can
   overflow the QThreadPool worker stack. Both encodings are depth-scanned
   before parsing; anything deeper than this limit is rejected without
   being parsed. */
inline static constexpr size_t MAX_PAYLOAD_NESTING_DEPTH = 512;

inline static bool jsonNestingWithinLimit(const std::string &payload)
{
	size_t depth = 0;
	bool inString = false;

	for (size_t i = 0; i < payload.size(); i++) {
		char c = payload[i];
		if (inString) {
			if (c == '\\')
				i++;
			else if (c == '"')
				inString = false;
			continue;
		}
		if (c == '"')
			inString = true;
		else if (c == '{' || c == '[') {
			if (++depth > MAX_PAYLOAD_NESTING_DEPTH)
				return false;
		} else if (c == '}' || c == ']') {
			if (depth > 0)
				depth--;
		}
	}
	return true;
}

/* Returns the size in bytes of the msgpack element header at `pos`
   (including the inline payload of scalars/strings), or 0 when the
   header is truncated or invalid. `childCount` receives the number of
   elements the token contains when it opens a container. */
inline static size_t msgpackElementSize(const std::string &payload, size_t pos, uint64_t &childCount)
{
	const unsigned char *data = reinterpret_cast<const unsigned char *>(payload.data());
	size_t left = payload.size() - pos;
	uint8_t b = data[pos];
	childCount = 0;

	auto take = [left](size_t n) {
		return n <= left ? n : size_t(0);
	};
	auto be16 = [&data, pos]() {
		return (uint32_t(data[pos + 1]) << 8) | data[pos + 2];
	};
	auto be32 = [&data, pos]() {
		return (uint64_t(data[pos + 1]) << 24) | (uint64_t(data[pos + 2]) << 16) | (uint64_t(data[pos + 3]) << 8) |
		       data[pos + 4];
	};

	if (b <= 0x7f || b >= 0xe0)
		return take(1);
	if (b >= 0x80 && b <= 0x8f) {
		childCount = uint64_t(b & 0x0f) * 2;
		return take(1);
	}
	if (b >= 0x90 && b <= 0x9f) {
		childCount = b & 0x0f;
		return take(1);
	}
	if (b >= 0xa0 && b <= 0xbf)
		return take(1 + (b & 0x1f));

	switch (b) {
	case 0xc0:
	case 0xc2:
	case 0xc3:
		return take(1);
	case 0xc4:
		return left >= 2 ? take(2 + data[pos + 1]) : 0;
	case 0xc5:
		return left >= 3 ? take(3 + be16()) : 0;
	case 0xc6:
		return left >= 5 ? take(5 + be32()) : 0;
	case 0xc7:
		return left >= 2 ? take(3 + data[pos + 1]) : 0;
	case 0xc8:
		return left >= 3 ? take(4 + be16()) : 0;
	case 0xc9:
		return left >= 5 ? take(6 + be32()) : 0;
	case 0xca:
		return take(5);
	case 0xcb:
		return take(9);
	case 0xcc:
		return take(2);
	case 0xcd:
		return take(3);
	case 0xce:
		return take(5);
	case 0xcf:
		return take(9);
	case 0xd0:
		return take(2);
	case 0xd1:
		return take(3);
	case 0xd2:
		return take(5);
	case 0xd3:
		return take(9);
	case 0xd4:
		return take(3);
	case 0xd5:
		return take(4);
	case 0xd6:
		return take(6);
	case 0xd7:
		return take(10);
	case 0xd8:
		return take(18);
	case 0xd9:
		return left >= 2 ? take(2 + data[pos + 1]) : 0;
	case 0xda:
		return left >= 3 ? take(3 + be16()) : 0;
	case 0xdb:
		return left >= 5 ? take(5 + be32()) : 0;
	case 0xdc:
		if (left < 3)
			return 0;
		childCount = be16();
		return 3;
	case 0xdd:
		if (left < 5)
			return 0;
		childCount = be32();
		return 5;
	case 0xde:
		if (left < 3)
			return 0;
		childCount = uint64_t(be16()) * 2;
		return 3;
	case 0xdf:
		if (left < 5)
			return 0;
		childCount = be32() * 2;
		return 5;
	default:
		return 0;
	}
}

inline static bool msgpackNestingWithinLimit(const std::string &payload)
{
	/* Each entry is the number of elements still expected inside one
	   open container; its size is the current nesting depth. */
	std::vector<uint64_t> remaining;

	for (size_t pos = 0; pos < payload.size();) {
		while (!remaining.empty() && remaining.back() == 0)
			remaining.pop_back();

		uint64_t childCount;
		size_t elementSize = msgpackElementSize(payload, pos, childCount);
		if (elementSize == 0)
			break; // Truncated or invalid input fails in the real parser.

		if (!remaining.empty())
			remaining.back()--;

		if (childCount > 0) {
			remaining.push_back(childCount);
			if (remaining.size() > MAX_PAYLOAD_NESTING_DEPTH)
				return false;
		}
		pos += elementSize;
	}
	return true;
}
