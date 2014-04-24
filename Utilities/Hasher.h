#pragma once

#include <cstdint>

namespace rpcs3 {
namespace utility {
namespace hashing {

	/** Used to store 32-bit hash values */
	typedef uint32_t HashValue32;

	/** Used to store 128-bit hash values */
	struct HashValue128 {
		uint32_t hash0;
		uint32_t hash1;
		uint32_t hash2;
		uint32_t hash3;

		HashValue128() : hash0(0), hash1(0), hash2(0), hash3(0) {}
		bool operator==(const HashValue128& rhs) {
			return	(hash0 == rhs.hash0) &&
				(hash1 == rhs.hash1) &&
				(hash2 == rhs.hash2) &&
				(hash3 == rhs.hash3);
		};
	};

	/** MurmurHash3 with 32-bit result */
	void Murmur3_32(const void * key, int len, uint32_t seed, void* out);

	/** MurmurHash3 with 128 bit output, x64 optimized */
	void Murmur3_128(const char* key, uint32_t len, uint32_t seed, void* out);

} // namespace hashing
} // namespace utility
} // namespace rpcs3