#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace DPORAM
{
	class UtilityMuTest : public testing::TestWithParam<tuple<double, number, number, number>>
	{
	};

	TEST_P(UtilityMuTest, OptimalMu)
	{
		auto beta	 = get<0>(GetParam());
		auto k		 = get<1>(GetParam());
		auto N		 = get<2>(GetParam());
		auto epsilon = get<3>(GetParam());

		auto nodesExp = ceil(log(k - 1) / log(k) + log(N) / log(k) - 1);
		auto nodes	  = (pow(k, nodesExp) - 1) / (k - 1) + N;

		auto predicate = [beta, k, N, epsilon, nodes](double mu) -> bool {
			return pow(1 - 0.5 * exp(-(mu * epsilon) / (log(N) / log(k))), nodes) <= 1 - beta;
		};

		auto expected = 0.0;
		while (predicate(expected))
		{
			expected += 1.0;
		}

		auto actual = optimalMu(beta, k, N, epsilon);

		EXPECT_EQ((number)expected, actual);
	}

	vector<tuple<double, number, number, number>> cases = {
		{0.001, 16, 1000000, 10},
		{0.0001, 16, 10000000, 100}};

	INSTANTIATE_TEST_SUITE_P(UtilityMuSuite, UtilityMuTest, testing::ValuesIn(cases));
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
