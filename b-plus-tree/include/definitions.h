#pragma once

#include <climits>
#include <vector>

// change to run all tests from different seed
#define TEST_SEED 0x13

namespace BPlusTree
{
	// defines the integer type block ID
	// change (e.g. to unsigned int) if needed
	using number = unsigned long long;
	using uchar  = unsigned char;
	using uint   = unsigned int;
	using bytes  = std::vector<uchar>;
}
