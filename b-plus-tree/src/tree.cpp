#include "tree.hpp"

#include "utility.hpp"

#include <algorithm>
#include <math.h>

namespace BPlusTree
{
	using namespace std;
	using boost::format;

	Tree::Tree(AbsStorageAdapter *storage) :
		storage(storage)
	{
	}

	Tree::Tree(AbsStorageAdapter *storage, vector<pair<number, bytes>> data) :
		storage(storage)
	{
		b = storage->getBlockSize() / sizeof(number) - 2;

		auto metaLocation = storage->malloc();

		sort(data.begin(), data.end(), [](pair<number, bytes> a, pair<number, bytes> b) { return a.first < b.first; });

		vector<pair<number, number>> layer;
		layer.resize(data.size());

		for (int i = data.size() - 1; i >= 0; i--)
		{
			layer[i].first  = data[i].first;
			layer[i].second = createDataBlock(
				data[i].second,
				(unsigned int)i == data.size() - 1 ? storage->empty() : layer[i + 1].second);
		}

		leftmostDataBlock = layer[0].second;
	}

	number Tree::createNodeBlock(vector<pair<number, number>> data)
	{
		if (storage->getBlockSize() - sizeof(number) < data.size() * 2 * sizeof(number))
		{
			throw Exception(boost::format("data size (%1% pairs) is too big for block size (%2%)") % data.size() % (storage->getBlockSize() - sizeof(number)));
		}

		number numbers[data.size() * 2 + 1];

		numbers[0] = data.size() * 2 * sizeof(number);
		for (unsigned int i = 0; i < data.size(); i++)
		{
			numbers[1 + 2 * i]	 = data[i].first;
			numbers[1 + 2 * i + 1] = data[i].second;
		}
		bytes block((uchar *)numbers, (uchar *)numbers + (data.size() * 2 + 1) * sizeof(number));
		block.resize(storage->getBlockSize());

		auto address = storage->malloc();
		storage->set(address, block);

		return address;
	}

	vector<pair<number, number>> Tree::readNodeBlock(number address)
	{
		auto read = storage->get(address);

		auto deconstructed = deconstruct(read, {sizeof(number)});
		auto size		   = numberFromBytes(deconstructed[0]);
		auto blockData	 = deconstructed[1];

		blockData.resize(size);

		auto count = blockData.size() / sizeof(number);

		uchar buffer[count * sizeof(number)];
		copy(blockData.begin(), blockData.end(), buffer);

		vector<pair<number, number>> result;
		result.resize(count / 2);
		for (unsigned int i = 0; i < count / 2; i++)
		{
			result[i].first  = ((number *)buffer)[2 * i];
			result[i].second = ((number *)buffer)[2 * i + 1];
		}

		return result;
	}

	number Tree::createDataBlock(bytes data, number next)
	{
		// 3 numbers are reserved:
		//	pointer to next block of the same data (if not the last such block)
		//	pointer to next data block (if the last such block)
		//	size of the user's data in a block (usefull for the last block)
		auto userBlockSize = storage->getBlockSize() - 3 * sizeof(number);

		auto blocks = (data.size() + userBlockSize - 1) / userBlockSize;
		vector<number> addresses;
		addresses.resize(blocks);
		for (unsigned int i = 0; i < blocks; i++)
		{
			addresses[i] = storage->malloc();
		}

		for (number i = 0; i < blocks; i++)
		{
			bytes buffer;
			auto end = min(userBlockSize * (i + 1), (number)data.size());

			copy(data.begin() + i * userBlockSize, data.begin() + end, back_inserter(buffer));
			buffer.resize(userBlockSize);

			auto thisSize   = end - i * userBlockSize;
			auto nextBlock  = i < blocks - 1 ? addresses[i + 1] : storage->empty();
			auto nextBucket = i < blocks - 1 ? storage->empty() : next;
			auto numbers	= concatNumbers(3, thisSize, nextBlock, nextBucket);
			storage->set(
				addresses[i],
				concat(2, &numbers, &buffer));
		}

		return addresses[0];
	}

	pair<bytes, number> Tree::readDataBlock(number address)
	{
		bytes data;

		while (true)
		{
			auto read		   = storage->get(address);
			auto deconstructed = deconstruct(read, {3 * sizeof(number)});
			auto numbers	   = deconstructNumbers(deconstructed[0]);
			auto blockData	 = deconstructed[1];

			auto thisSize   = numbers[0];
			auto nextBlock  = numbers[1];
			auto nextBucket = numbers[2];

			blockData.resize(thisSize);
			data = concat(2, &data, &blockData);

			if (nextBlock != storage->empty())
			{
				address = nextBlock;
				continue;
			}
			else
			{
				return {data, nextBucket};
			}
		}
	}
}
