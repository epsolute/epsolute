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

	TEST_F(UtilityTest, FromToNumber)
	{
		ASSERT_EQ(56uLL, numberFromBytes(bytesFromNumber(56uLL)));
	}

	TEST_F(UtilityTest, Concat)
	{
		bytes first{0x02, 0x04};
		bytes second{0x06, 0x08, 0x0a};
		bytes third{0x0c};

		auto concatenated = concat(3, &first, &second, &third);

		ASSERT_EQ(bytes({0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c}), concatenated);
	}

	TEST_F(UtilityTest, ConcatNumbers)
	{
		number first  = 0uLL;
		number second = ULONG_MAX;

		auto concatenated = concatNumbers(2, first, second);

		ASSERT_EQ(bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}), concatenated);
	}

	TEST_F(UtilityTest, ConcatWithNumbers)
	{
		bytes first{0x02, 0x04};
		number second = ULONG_MAX;
		bytes third{0x0c};

		auto secondBytes  = bytesFromNumber(second);
		auto concatenated = concat(3, &first, &secondBytes, &third);

		ASSERT_EQ(bytes({0x02, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0c}), concatenated);
	}

	TEST_F(UtilityTest, Deconstruct)
	{
		bytes first{0x02, 0x04};
		bytes second{0x06, 0x08, 0x0a};
		bytes third{0x0c};

		auto concatenated  = concat(3, &first, &second, &third);
		auto deconstructed = deconstruct(concatenated, {2, 5});

		ASSERT_EQ(3, deconstructed.size());
		EXPECT_EQ(first, deconstructed[0]);
		EXPECT_EQ(second, deconstructed[1]);
		EXPECT_EQ(third, deconstructed[2]);
	}
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
