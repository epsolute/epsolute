#include "definitions.h"
#include "storage-adapter.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace BPlusTree
{
	enum TestingStorageAdapterType
	{
		StorageAdapterTypeInMemory,
		StorageAdapterTypeFileSystem
	};

	class StorageAdapterTest : public testing::TestWithParam<TestingStorageAdapterType>
	{
		public:
		inline static const number BLOCK_SIZE = 32;
		inline static const string FILE_NAME  = "storage.bin";

		protected:
		AbsStorageAdapter* adapter;

		StorageAdapterTest()
		{
			auto type = GetParam();
			switch (type)
			{
				case StorageAdapterTypeInMemory:
					adapter = new InMemoryStorageAdapter(BLOCK_SIZE);
					break;
				case StorageAdapterTypeFileSystem:
					adapter = new FileSystemStorageAdapter(BLOCK_SIZE, FILE_NAME, true);
					break;
				default:
					throw Exception(boost::format("TestingStorageAdapterType %1% is not implemented") % type);
			}
		}

		~StorageAdapterTest() override
		{
			delete adapter;
			remove(FILE_NAME.c_str());
		}
	};

	TEST_P(StorageAdapterTest, Initialization)
	{
		SUCCEED();
	}

	TEST_P(StorageAdapterTest, NoOverrideFile)
	{
		if (GetParam() == StorageAdapterTypeFileSystem)
		{
			auto before		= fromText("before", BLOCK_SIZE);
			auto after		= fromText("after", BLOCK_SIZE);
			string filename = "tmp.bin";

			auto storage	   = new FileSystemStorageAdapter(BLOCK_SIZE, filename, true);
			auto addressBefore = storage->malloc();
			storage->set(addressBefore, before);

			ASSERT_EQ(before, storage->get(addressBefore));

			delete storage;

			storage			  = new FileSystemStorageAdapter(BLOCK_SIZE, filename, false);
			auto addressAfter = storage->malloc();
			storage->set(addressAfter, after);

			ASSERT_EQ(before, storage->get(addressBefore));
			ASSERT_EQ(after, storage->get(addressAfter));

			remove(filename.c_str());
		}
		else
		{
			SUCCEED();
		}
	}

	TEST_P(StorageAdapterTest, SetGetNoExceptions)
	{
		bytes data;
		data.resize(BLOCK_SIZE);

		ASSERT_NO_THROW({
			auto address = adapter->malloc();
			adapter->set(address, data);
			adapter->get(address);
		});
	}

	TEST_P(StorageAdapterTest, InvalidAddress)
	{
		ASSERT_ANY_THROW(adapter->set(5uLL, bytes()));
	}

	TEST_P(StorageAdapterTest, WrongDataSize)
	{
		auto address = adapter->malloc();
		bytes data;

		data.resize(BLOCK_SIZE - 1);
		ASSERT_ANY_THROW(adapter->set(address, data));

		data.resize(BLOCK_SIZE + 1);
		ASSERT_ANY_THROW(adapter->set(address, data));
	}

	TEST_P(StorageAdapterTest, ReadWhatWasWritten)
	{
		auto data = fromText("hello", BLOCK_SIZE);

		auto address = adapter->malloc();
		adapter->set(address, data);
		auto returned = adapter->get(address);

		ASSERT_EQ(data, returned);
	}

	string printTestName(testing::TestParamInfo<TestingStorageAdapterType> input)
	{
		switch (input.param)
		{
			case StorageAdapterTypeInMemory:
				return "InMemory";
			case StorageAdapterTypeFileSystem:
				return "FileSystem";
			default:
				throw Exception(boost::format("TestingStorageAdapterType %1% is not implemented") % input.param);
		}
	}

	INSTANTIATE_TEST_SUITE_P(StorageAdapterSuite, StorageAdapterTest, testing::Values(StorageAdapterTypeInMemory, StorageAdapterTypeFileSystem), printTestName);
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
