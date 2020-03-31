#include "definitions.h"
#include "tree.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"
#include <boost/algorithm/string.hpp>

using namespace std;

#define ASSERT_THROW_CONTAINS(statement, substring)                                                                     \
	try                                                                                                                 \
	{                                                                                                                   \
		statement;                                                                                                      \
		FAIL() << "statement did not throw exception";                                                                  \
	}                                                                                                                   \
	catch (Exception exception)                                                                                         \
	{                                                                                                                   \
		if (!boost::icontains(exception.what(), substring))                                                             \
		{                                                                                                               \
			FAIL() << "exception message does not contain '" << substring << "', the message is: " << exception.what(); \
		}                                                                                                               \
	}                                                                                                                   \
	catch (...)                                                                                                         \
	{                                                                                                                   \
		FAIL() << "statement threw unexpected exception";                                                               \
	}

namespace BPlusTree
{
	bytes generateDataBytes(string word, int size);
	vector<pair<number, bytes>> generateDataPoints(int from, int to, int size);
	pair<number, vector<pair<number, number>>> generatePairs(number BLOCK_SIZE);

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

		vector<pair<number, bytes>> populateTree(int from = 5, int to = 15, int size = 100)
		{
			auto data = generateDataPoints(from, to, size);
			tree	  = new Tree(storage, data);

			return data;
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

	pair<number, vector<pair<number, number>>> generatePairs(number BLOCK_SIZE)
	{
		auto count = (BLOCK_SIZE - sizeof(number)) / sizeof(number) / 2;

		vector<pair<number, number>> pairs;
		pairs.resize(count);
		for (unsigned int i = 0; i < count; i++)
		{
			pairs[i].first  = i;
			pairs[i].second = i * 1000;
		}

		return {count, pairs};
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
			auto [type, read] = tree->checkType(current);
			ASSERT_EQ(DataBlock, type);

			auto [payload, key, next] = tree->readDataBlock(read);
			ASSERT_EQ(size, payload.size());
			auto block = find_if(
				data.begin(),
				data.end(),
				[i](const pair<number, bytes>& val) {
					return val.first == i;
				});
			current = next;

			ASSERT_EQ((*block).second, payload);
			ASSERT_EQ(i, key);
		}

		delete tree;
	}

	TEST_F(TreeTest, CreateNodeBlockTooBig)
	{
		tree = new Tree(storage);
		vector<pair<number, number>> pairs;
		pairs.resize(BLOCK_SIZE / 2);

		ASSERT_ANY_THROW(tree->createNodeBlock(pairs));

		delete tree;
	}

	TEST_F(TreeTest, CreateNodeBlock)
	{
		tree = new Tree(storage);

		auto pairs = generatePairs(BLOCK_SIZE).second;

		ASSERT_NO_THROW(tree->createNodeBlock(pairs));

		delete tree;
	}

	TEST_F(TreeTest, ReadNodeBlock)
	{
		tree = new Tree(storage);

		auto pairs = generatePairs(BLOCK_SIZE).second;

		auto address	   = tree->createNodeBlock(pairs);
		auto [type, block] = tree->checkType(address);
		ASSERT_EQ(NodeBlock, type);
		auto read = tree->readNodeBlock(block);

		ASSERT_EQ(pairs, read);

		delete tree;
	}

	TEST_F(TreeTest, PushLayer)
	{
		tree = new Tree(storage);

		auto [count, pairs] = generatePairs(BLOCK_SIZE * 2);

		auto pushed  = tree->pushLayer(pairs);
		auto counter = 0;
		for (unsigned int i = 0; i < pushed.size(); i++)
		{
			auto [type, read] = tree->checkType(pushed[i].second);
			ASSERT_EQ(NodeBlock, type);

			auto block = tree->readNodeBlock(read);
			for (auto key : block)
			{
				EXPECT_LE(key.first, pushed[i].first);
				EXPECT_EQ(key.first * 1000, key.second);
				counter++;
			}
		}
		EXPECT_EQ(count, counter);

		delete tree;
	}

	TEST_F(TreeTest, BasicSearch)
	{
		const auto query = 10uLL;

		auto data = populateTree();

		auto returned = tree->search(query);
		auto expected = find_if(
			data.begin(),
			data.end(),
			[](const pair<number, bytes>& val) {
				return val.first == query;
			});

		ASSERT_EQ((*expected).second, returned);

		delete tree;
	}

	TEST_F(TreeTest, ConsistencyCheck)
	{
		populateTree();

		ASSERT_NO_THROW(tree->checkConsistency());

		delete tree;
	}

	TEST_F(TreeTest, ConsistencyCheckWrongBlockType)
	{
		populateTree();

		auto root = storage->get(tree->root);
		root[0]   = 0xff;
		storage->set(tree->root, root);

		ASSERT_THROW_CONTAINS(tree->checkConsistency(), "block type");
	}

	TEST_F(TreeTest, ConsistencyCheckDataBlockPointer)
	{
		populateTree();

		auto dataBlock				  = storage->get(tree->leftmostDataBlock);
		dataBlock[sizeof(number) * 2] = storage->empty();
		storage->set(tree->leftmostDataBlock, dataBlock);

		ASSERT_THROW_CONTAINS(tree->checkConsistency(), "data block");
	}

	TEST_F(TreeTest, ConsistencyCheckDataBlockKey)
	{
		populateTree();

		auto dataBlock				  = storage->get(tree->leftmostDataBlock);
		dataBlock[sizeof(number) * 3] = 0uLL;
		storage->set(tree->leftmostDataBlock, dataBlock);

		ASSERT_THROW_CONTAINS(tree->checkConsistency(), "key");
	}
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
