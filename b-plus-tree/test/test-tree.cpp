#include "definitions.h"
#include "tree.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace BPlusTree
{
	class TreeTest : public testing::Test
	{
		public:
		inline static const number BLOCK_SIZE = 64;

		protected:
		Tree* tree;
		AbsStorageAdapter* storage = new InMemoryStorageAdapter(BLOCK_SIZE);

		~TreeTest() override
		{
			delete storage;
		}
	};

	bytes generateDataBytes(string word, int size)
	{
		stringstream ss;
		for (int i = 0; i < size; i += word.size())
		{
			ss << word;
		}

		return fromText(ss.str(), size);
	}

	vector<pair<number, bytes>> generateDataPoints(int from, int to, int size)
	{
		vector<pair<number, bytes>> data;
		for (int i = from; i <= to; i++)
		{
			data.push_back({i, generateDataBytes(to_string(i), size)});
		}

		return data;
	}

	TEST_F(TreeTest, Initialization)
	{
		auto data = generateDataPoints(5, 7, 100);

		ASSERT_NO_THROW(new Tree(storage, data));
	}

	TEST_F(TreeTest, ReadDataLayer)
	{
		const auto from = 5;
		const auto to   = 7;
		const auto size = 100;

		auto data = generateDataPoints(from, to, size);

		tree = new Tree(storage, data);

		auto current = tree->leftmostDataBlock;
		for (unsigned int i = from; i <= to; i++)
		{
			auto [payload, next] = tree->readDataBlock(current);
			ASSERT_EQ(size, payload.size());
			auto block = find_if(
				data.begin(),
				data.end(),
				[i](const pair<number, bytes>& val) {
					return val.first == i;
				});
			current = next;

			ASSERT_EQ((*block).second, payload);
		}

		delete tree;
	}
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
