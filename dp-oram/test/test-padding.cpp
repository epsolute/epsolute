#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace DPORAM
{
	class UtilityPaddingTest : public testing::TestWithParam<tuple<number, number, number>>
	{
	};

	TEST_P(UtilityPaddingTest, Padding)
	{
		auto [min, max, buckets] = GetParam();
		auto const domain		 = max - min;

		for (auto from = min; from <= max; from++)
		{
			for (auto to = from; to <= max; to++)
			{
				auto [fromBucket, toBucket, left, right] = padToBuckets({from, to}, min, max, buckets);

				EXPECT_LT(toBucket, buckets);
				EXPECT_GE(fromBucket, 0);

				EXPECT_GE(toBucket, fromBucket);
				EXPECT_GT(right, left);

				EXPECT_LE(left, from);
				EXPECT_GE(right, to);

				auto diff		= to - from;
				auto bucketDiff = toBucket - fromBucket + 1;
				EXPECT_LE(diff, bucketDiff * domain / (double)buckets);

				EXPECT_NEAR((double)left / domain, (double)fromBucket / buckets, 0.1);
				EXPECT_NEAR((double)right / domain, (double)(toBucket + 1) / buckets, 0.1);
			}
		}
	}

	vector<tuple<number, number, number>> cases = {
		{0, 106, 7},
		{0, 999, 256},
	};

	string printTestName(testing::TestParamInfo<tuple<number, number, number>> input)
	{
		auto [min, max, buckets] = input.param;
		return boost::str(boost::format("min%1%max%2%buckets%3%") % min % max % buckets);
	}

	INSTANTIATE_TEST_SUITE_P(UtilityPaddingSuite, UtilityPaddingTest, testing::ValuesIn(cases), printTestName);
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
