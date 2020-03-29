#include "tree.hpp"

#include "utility.hpp"

#include <algorithm>
#include <math.h>

namespace BPlusTree
{
	using namespace std;

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

		for (unsigned int i = data.size() - 1; i >= 0; i--)
		{
			layer[i].first  = data[i].first;
			layer[i].second = createDataBlock(
				data[i].second,
				i == data.size() - 1 ? storage->empty() : layer[i + 1].second);
		}

		leftmostDataBlock = layer[0].second;
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

			auto thisSize   = bytesFromNumber(end - i * userBlockSize);
			auto nextBlock  = bytesFromNumber(i < blocks - 1 ? addresses[i + 1] : storage->empty());
			auto nextBucket = bytesFromNumber(i < blocks - 1 ? storage->empty() : next);
			storage->set(
				addresses[i],
				concat(4, &thisSize, &nextBlock, &nextBucket, &buffer));
		}

		return addresses[0];
	}

	pair<bytes, number> Tree::readDataBlock(number address)
	{
		bytes data;

		while (true)
		{
			auto read		   = storage->get(address);
			auto deconstructed = deconstruct(read, {sizeof(number), 2 * sizeof(number), 3 * sizeof(number)});
			auto thisSize	  = numberFromBytes(deconstructed[0]);
			auto nextBlock	 = numberFromBytes(deconstructed[1]);
			auto nextBucket	= numberFromBytes(deconstructed[2]);
			auto blockData	 = deconstructed[3];

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
