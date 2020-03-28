#include "definitions.h"
#include "stash-adapter.hpp"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace PathORAM
{
	class StashAdapterTest : public ::testing::Test
	{
		public:
		inline static const number CAPACITY = 10;

		protected:
		InMemoryStashAdapter* adapter = new InMemoryStashAdapter(CAPACITY);

		~StashAdapterTest() override
		{
			delete adapter;
		}
	};

	TEST_F(StashAdapterTest, Initialization)
	{
		SUCCEED();
	}

	TEST_F(StashAdapterTest, ReadGetEraseNoCrash)
	{
		EXPECT_NO_THROW({
			adapter->add(5uLL, bytes());
			adapter->getAll();
			adapter->remove(5uLL);
		});
	}

	TEST_F(StashAdapterTest, GetAllShuffle)
	{
		for (number i = 0; i < CAPACITY; i++)
		{
			adapter->add(i, bytes());
		}

		auto fist   = adapter->getAll();
		auto second = adapter->getAll();

		EXPECT_EQ(fist.size(), second.size());
		EXPECT_NE(fist, second);
	}

	TEST_F(StashAdapterTest, OverflowAdd)
	{
		for (number i = 0uLL; i < CAPACITY; i++)
		{
			adapter->add(i, bytes());
		}
		ASSERT_ANY_THROW(adapter->add(CAPACITY + 1, bytes()));
		adapter->remove(0uLL);
		ASSERT_NO_THROW(adapter->add(CAPACITY + 1, bytes()));
		ASSERT_NO_THROW(adapter->add(CAPACITY + 1, bytes())); // duplicate key should not be inserted
	}

	TEST_F(StashAdapterTest, OverflowUpdate)
	{
		for (number i = 0uLL; i < CAPACITY; i++)
		{
			adapter->update(i, bytes());
		}
		ASSERT_ANY_THROW(adapter->update(CAPACITY + 1, bytes()));
		adapter->remove(0uLL);
		ASSERT_NO_THROW(adapter->update(CAPACITY + 1, bytes()));
		ASSERT_NO_THROW(adapter->update(CAPACITY + 1, bytes())); // duplicate key should not be inserted
	}

	TEST_F(StashAdapterTest, ReadWhatWasWritten)
	{
		auto block = CAPACITY - 1;
		auto data  = bytes{0x25};

		adapter->add(block, data);
		auto returned = adapter->get(block);

		ASSERT_EQ(data, returned);
	}

	TEST_F(StashAdapterTest, Override)
	{
		auto block = CAPACITY - 1;
		auto old = bytes{0x25}, _new = bytes{0x56};

		adapter->add(block, old);
		adapter->update(block, _new);

		auto returned = adapter->get(block);

		ASSERT_EQ(1, adapter->getAll().size());
		ASSERT_EQ(_new, returned);
	}

	TEST_F(StashAdapterTest, NoOverride)
	{
		auto block = CAPACITY - 1;
		auto old = bytes{0x25}, _new = bytes{0x56};

		adapter->add(block, old);
		adapter->add(block, _new);

		auto returned = adapter->get(block);

		ASSERT_EQ(1, adapter->getAll().size());
		ASSERT_EQ(old, returned);
	}
}

int main(int argc, char** argv)
{
	srand(TEST_SEED);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
