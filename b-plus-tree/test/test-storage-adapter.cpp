#include "definitions.h"
#include "storage-adapter.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace BPlusTree
{
	class StorageAdapterTest : public testing::Test
	{
		public:
		inline static const number BLOCK_SIZE = 32;

		protected:
		AbsStorageAdapter* adapter = new InMemoryStorageAdapter(BLOCK_SIZE);

		~StorageAdapterTest() override
		{
			delete adapter;
		}
	};

	TEST_F(StorageAdapterTest, Initialization)
	{
		SUCCEED();
	}

	TEST_F(StorageAdapterTest, SetGetNoExceptions)
	{
		bytes data;
		data.resize(BLOCK_SIZE);

		ASSERT_NO_THROW({
			auto address = adapter->malloc();
			adapter->set(address, data);
			adapter->get(address);
		});
	}

	TEST_F(StorageAdapterTest, InvalidAddress)
	{
		ASSERT_ANY_THROW(adapter->set(5uLL, bytes()));
	}

	TEST_F(StorageAdapterTest, WrongDataSize)
	{
		auto address = adapter->malloc();
		bytes data;

		data.resize(BLOCK_SIZE - 1);
		ASSERT_ANY_THROW(adapter->set(address, data));

		data.resize(BLOCK_SIZE + 1);
		ASSERT_ANY_THROW(adapter->set(address, data));
	}

	TEST_F(StorageAdapterTest, ReadWhatWasWritten)
	{
		auto data = fromText("hello", BLOCK_SIZE);

		auto address = adapter->malloc();
		adapter->set(address, data);
		auto returned = adapter->get(address);

		ASSERT_EQ(data, returned);
	}
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
