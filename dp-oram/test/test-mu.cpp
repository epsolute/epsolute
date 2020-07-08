#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace DPORAM
{
	class UtilityMuTest : public testing::TestWithParam<tuple<double, number, number, double, number, number>>
	{
	};

	TEST_P(UtilityMuTest, OptimalMu)
	{
		auto beta	 = get<0>(GetParam());
		auto k		 = get<1>(GetParam());
		auto N		 = get<2>(GetParam());
		auto epsilon = get<3>(GetParam());
		auto levels	 = get<4>(GetParam());
		auto orams	 = get<5>(GetParam());

		levels = min(levels, (number)(log(N) / log(k)));

		auto nodes	 = 0uLL;
		auto atLevel = (number)(log(N) / log(k));
		for (auto level = 0uLL; level <= levels; level++)
		{
			nodes += atLevel;
			atLevel /= k;
		}
		nodes *= orams;

		auto predicate = [beta, epsilon, levels, nodes](double mu) -> bool {
			return pow(1 - 0.5 * exp(-(mu * epsilon) / (double)levels), nodes) <= 1 - beta;
		};

		auto expected = 0.0;
		while (predicate(expected))
		{
			expected += 1.0;
		}

		auto actual = optimalMu(beta, k, N, epsilon, levels, orams);

		EXPECT_EQ((number)expected, actual);
	}

	vector<tuple<double, number, number, double, number, number>> cases = {
		{0.001, 16, 1000000, 1.0, ULONG_MAX, 1},
		{0.0001, 16, 10000000, 0.69, ULONG_MAX, 1},
		{1.0 / (1 << 20), 16, 65536, 1.1, ULONG_MAX, 1},
		{1.0 / (1 << 20), 16, 1048576, 1.0, ULONG_MAX, 1},
		{0.001, 16, 1000000, 0.1, ULONG_MAX, 64},
		{0.0001, 16, 10000000, 1.0, ULONG_MAX, 64},
		{1.0 / (1 << 20), 16, 65536, 0.69, ULONG_MAX, 64},
		{1.0 / (1 << 20), 16, 1048576, 1.0, ULONG_MAX, 64}};

	INSTANTIATE_TEST_SUITE_P(UtilityMuSuite, UtilityMuTest, testing::ValuesIn(cases));
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
