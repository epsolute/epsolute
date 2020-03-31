#include "definitions.h"
#include "tree.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"
#include <boost/algorithm/string.hpp>

using namespace std;

namespace BPlusTree
{
	class TreeTestBig : public testing::TestWithParam<pair<number, number>>
	{
		public:
		inline static number BLOCK_SIZE;
		inline static number COUNT;

		protected:
		Tree* tree;
		AbsStorageAdapter* storage;

		~TreeTestBig() override
		{
			delete storage;
		}

		TreeTestBig()
		{
			auto [BLOCK_SIZE, COUNT] = GetParam();
			this->BLOCK_SIZE		 = BLOCK_SIZE;
			this->COUNT				 = COUNT;

			storage = new InMemoryStorageAdapter(BLOCK_SIZE);
		}
	};

	bytes random(int size)
	{
		uchar material[size];
		for (int i = 0; i < size; i++)
		{
			material[i] = (uchar)rand();
		}
		return bytes(material, material + size);
	}

	vector<bytes> getExpected(vector<pair<number, bytes>> data, number start, number end)
	{
		vector<bytes> expected;
		auto i = data.begin();
		while (i != data.end())
		{
			i = find_if(
				i,
				data.end(),
				[start, end](const pair<number, bytes>& val) {
					return val.first >= start && val.first <= end;
				});
			if (i != data.end())
			{
				auto element = (*i).second;
				expected.push_back(element);
				i++;
			}
		}
		sort(expected.begin(), expected.end());
		return expected;
	}

	TEST_P(TreeTestBig, Simulation)
	{
		vector<pair<number, bytes>> data;
		for (number i = 0; i < COUNT; i++)
		{
			data.push_back({rand() % (COUNT / 3),
							random(BLOCK_SIZE * 3)});
		}

		tree = new Tree(storage, data);

		ASSERT_NO_THROW(tree->checkConsistency());

		for (number key = 0; key < COUNT / 3; key++)
		{
			auto returned = tree->search(key);
			sort(returned.begin(), returned.end());
			auto expected = getExpected(data, key, key);

			EXPECT_EQ(expected, returned);
		}

		for (number i = 0; i < COUNT; i++)
		{
			number start, end;
			while (true)
			{
				start = rand() % COUNT / 3;
				end	  = rand() % COUNT / 3;
				if (start < end)
				{
					break;
				}
			}

			auto returned = tree->search(start, end);
			sort(returned.begin(), returned.end());
			auto expected = getExpected(data, start, end);
			EXPECT_EQ(expected, returned);
		}

		auto returned = tree->search(0, COUNT / 3);
		sort(returned.begin(), returned.end());
		auto expected = getExpected(data, 0, COUNT / 3);
		EXPECT_EQ(expected, returned);
	}

	string printTestName(testing::TestParamInfo<pair<number, number>> input)
	{
		return boost::str(boost::format("%1%i%2%") % input.param.first % input.param.second);
	}

	pair<number, number> cases[] = {
		{64, 10},
		{64, 500},
		{128, 500},
		{256, 500},
	};

	INSTANTIATE_TEST_SUITE_P(TreeTestBigSuite, TreeTestBig, testing::ValuesIn(cases), printTestName);
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
