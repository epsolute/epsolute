#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace DPORAM
{
	class UtilityBRCTest : public testing::TestWithParam<tuple<number, number, number, number, vector<pair<number, number>>>>
	{
	};

	TEST_P(UtilityBRCTest, BRC)
	{
		auto [fanout, from, to, maxLevel, expected] = GetParam();

		auto actual = BRC(fanout, from, to, maxLevel);

		auto sortFunc = [](pair<number, number> a, pair<number, number> b) -> bool {
			if (a.first != b.first)
			{
				return a.first < b.first;
			}
			return a.second < b.second;
		};

		sort(expected.begin(), expected.end(), sortFunc);
		sort(actual.begin(), actual.end(), sortFunc);

		EXPECT_EQ(expected, actual);
	}

	vector<tuple<number, number, number, number, vector<pair<number, number>>>> cases()
	{
		vector<tuple<number, number, number, number, vector<pair<number, number>>>> result =
			{
				{3, 2, 7, ULONG_MAX, {{0, 2}, {1, 1}, {0, 6}, {0, 7}}},
				{3, 0, 8, ULONG_MAX, {{2, 0}}},
				{3, 3, 5, ULONG_MAX, {{1, 1}}},
				{3, 3, 3, ULONG_MAX, {{0, 3}}},
				{3, 2, 7, 0, {{0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}}},
				{3, 0, 8, 1, {{1, 0}, {1, 1}, {1, 2}}},
			};

		// tests from ORE benchmark BRC
		// https://git.dbogatov.org/bu/ore-benchmark/Project-Code/-/blob/master/test/Simulators/Protocols/SSE/BRCTests.cs#L64
		vector<tuple<number, number, number, vector<string>>> binaryCases{
			{2, 7, 4, {"001", "01"}},
			{1, 7, 4, {"0001", "001", "01"}},
			{4, 5, 4, {"010"}},
			{3, 12, 4, {"01", "0011", "10", "1100"}},
			{11, 12, 4, {"1011", "1100"}},
			{0, 15, 4, {""}},
			{0, 7, 4, {"0"}},
			{1, 14, 4, {"0001", "001", "01", "10", "110", "1110"}},
			{1, 1, 4, {"0001"}}};

		for (auto [from, to, height, items] : binaryCases)
		{
			vector<pair<number, number>> expected;
			for (auto item : items)
			{
				auto level = height - item.size();
				auto value = item.size() > 0 ? stoi(item, 0, 2) : 0;
				expected.push_back({level, value});
			}
			result.push_back({2, from, to, ULONG_MAX, expected});
		}

		return result;
	}

	INSTANTIATE_TEST_SUITE_P(UtilityBRCSuite, UtilityBRCTest, testing::ValuesIn(cases()));
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
