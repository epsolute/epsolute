#include "definitions.h"
#include "tree.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"
#include <boost/algorithm/string.hpp>

using namespace std;

namespace BPlusTree
{
	enum TestingStorageAdapterType
	{
		StorageAdapterTypeInMemory,
		StorageAdapterTypeFileSystem
	};

	class TreeTestBig : public testing::TestWithParam<tuple<number, number, TestingStorageAdapterType>>
	{
		public:
		inline static number BLOCK_SIZE;
		inline static number COUNT;
		inline static const string FILE_NAME = "storage.bin";

		protected:
		Tree* tree;
		AbsStorageAdapter* storage;

		~TreeTestBig() override
		{
			delete storage;
			remove(FILE_NAME.c_str());
		}

		TreeTestBig()
		{
			auto [BLOCK_SIZE, COUNT, storageType] = GetParam();
			this->BLOCK_SIZE					  = BLOCK_SIZE;
			this->COUNT							  = COUNT;

			switch (storageType)
			{
				case StorageAdapterTypeInMemory:
					storage = new InMemoryStorageAdapter(BLOCK_SIZE);
					break;
				case StorageAdapterTypeFileSystem:
					storage = new FileSystemStorageAdapter(BLOCK_SIZE, FILE_NAME, true);
					break;
				default:
					throw Exception(boost::format("TestingStorageAdapterType %1% is not implemented") % storageType);
			}
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

	string printTestName(testing::TestParamInfo<tuple<number, number, TestingStorageAdapterType>> input)
	{
		auto [blockSize, count, type] = input.param;
		string typeStr;

		switch (type)
		{
			case StorageAdapterTypeInMemory:
				typeStr = "InMemory";
				break;
			case StorageAdapterTypeFileSystem:
				typeStr = "FileSystem";
				break;
			default:
				throw Exception(str(boost::format("TestingStorageAdapterType %1% is not implemented") % type));
		}

		return Exception(boost::format("%1%i%2%i%3%") % blockSize % count % typeStr);
	}

	vector<tuple<number, number, TestingStorageAdapterType>> cases()
	{
		vector<number> blockSizes				= {64, 128, 256};
		vector<number> counts					= {10, 500};
		vector<TestingStorageAdapterType> types = {StorageAdapterTypeFileSystem, StorageAdapterTypeInMemory};

		vector<tuple<number, number, TestingStorageAdapterType>> result;

		for (auto blockSize : blockSizes)
		{
			for (auto count : counts)
			{
				for (auto type : types)
				{
					result.push_back({blockSize, count, type});
				}
			}
		}

		return result;
	};

	INSTANTIATE_TEST_SUITE_P(TreeTestBigSuite, TreeTestBig, testing::ValuesIn(cases()), printTestName);
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
