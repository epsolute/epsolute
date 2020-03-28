#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace BPlusTree
{
	class UtilityTest : public ::testing::Test
	{
		protected:
		inline static const number BLOCK_SIZE = 32;
	};

	TEST_F(UtilityTest, FromToText)
	{
		ASSERT_EQ(toText(fromText("hello", BLOCK_SIZE), BLOCK_SIZE), "hello");
		ASSERT_EQ(BLOCK_SIZE, fromText("hello", BLOCK_SIZE).size());
	}
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
