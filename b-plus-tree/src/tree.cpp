#include "tree.hpp"

#include "utility.hpp"

#include <algorithm>
#include <math.h>

namespace BPlusTree
{
	using namespace std;
	using boost::format;

	pair<BlockType, number> getTypeSize(number typeAndSize);
	number setTypeSize(BlockType type, number size);

	Tree::Tree(AbsStorageAdapter *storage) :
		storage(storage)
	{
		b = (storage->getBlockSize() - sizeof(number)) / (2 * sizeof(number));
	}

	// TODO enable for signed

	Tree::Tree(AbsStorageAdapter *storage, vector<pair<number, bytes>> data) :
		Tree(storage)
	{
		sort(data.begin(), data.end(), [](pair<number, bytes> a, pair<number, bytes> b) { return a.first < b.first; });

		// data layer
		vector<pair<number, number>> layer;
		layer.resize(data.size());
		for (int i = data.size() - 1; i >= 0; i--)
		{
			layer[i].first  = data[i].first;
			layer[i].second = createDataBlock(
				data[i].second,
				data[i].first,
				(unsigned int)i == data.size() - 1 ? storage->empty() : layer[i + 1].second);
		}
		leftmostDataBlock = layer[0].second;

		// leaf layer
		layer = pushLayer(layer);

		// nodes layer
		while (layer.size() > 1)
		{
			layer = pushLayer(layer);
		}
		root = layer[0].second;
	}

	vector<bytes> Tree::search(number key)
	{
		return search(key, key);
	}

	vector<bytes> Tree::search(number start, number end)
	{
		auto address = root;
		while (true)
		{
			auto [type, read] = checkType(address);
			switch (type)
			{
				case NodeBlock:
				{
					auto block = readNodeBlock(read);
					address	= storage->empty();
					for (unsigned int i = 0; i < block.size(); i++)
					{
						if (start <= block[i].first)
						{
							address = block[i].second;
							break;
						}
					}
					if (address == storage->empty())
					{
						return vector<bytes>();
					}
					break;
				}
				case DataBlock:
				{
					vector<bytes> result;
					tuple<bytes, number, number> block;
					while (true)
					{
						auto block = readDataBlock(read);
						if (get<1>(block) < start || get<1>(block) > end || get<2>(block) == storage->empty())
						{
							return result;
						}
						result.push_back(get<0>(block));
						read = checkType(get<2>(block)).second;
					}
				}
			}
		}
	}

	// TODO document that number of pointers and keys is the same

	vector<pair<number, number>> Tree::pushLayer(vector<pair<number, number>> input)
	{
		vector<pair<number, number>> layer;
		for (unsigned int i = 0; i < input.size(); i += b)
		{
			vector<pair<number, number>> block;
			block.resize(b);
			number max = 0uLL; // TODO signed
			for (unsigned int j = 0; j < b; j++)
			{
				if (i + j < input.size())
				{
					block[j] = input[i + j];
					max		 = block[j].first > max ? block[j].first : max;
				}
				else
				{
					block.resize(j);
					break;
				}
			}
			auto address = createNodeBlock(block);
			layer.push_back({max, address});
		}

		return layer;
	}

	number Tree::createNodeBlock(vector<pair<number, number>> data)
	{
		if (storage->getBlockSize() - sizeof(number) < data.size() * 2 * sizeof(number))
		{
			throw Exception(boost::format("data size (%1% pairs) is too big for block size (%2%)") % data.size() % (storage->getBlockSize() - sizeof(number)));
		}

		number numbers[data.size() * 2 + 1];

		numbers[0] = setTypeSize(NodeBlock, data.size() * 2 * sizeof(number));
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

	vector<pair<number, number>> Tree::readNodeBlock(bytes block)
	{
		auto deconstructed = deconstruct(block, {sizeof(number)});
		auto [type, size]  = getTypeSize(numberFromBytes(deconstructed[0]));
		auto blockData	 = deconstructed[1];

		if (type != NodeBlock)
		{
			throw Exception("attempt to read a non-node block as node block");
		}

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

	number Tree::createDataBlock(bytes data, number key, number next)
	{
		auto firstBlockSize = storage->getBlockSize() - 4 * sizeof(number);
		auto otherBlockSize = storage->getBlockSize() - 2 * sizeof(number);

		auto blocks =
			data.size() <= firstBlockSize ?
				1 :
				1 + (data.size() - firstBlockSize + otherBlockSize - 1) / otherBlockSize;
		vector<number> addresses;
		addresses.resize(blocks);
		for (unsigned int i = 0; i < blocks; i++)
		{
			addresses[i] = storage->malloc();
		}

		auto readSoFar = 0;
		for (number i = 0; i < blocks; i++)
		{
			bytes buffer;
			auto end = min(readSoFar + (i == 0 ? firstBlockSize : otherBlockSize), (number)data.size());

			copy(data.begin() + readSoFar, data.begin() + end, back_inserter(buffer));
			buffer.resize(i == 0 ? firstBlockSize : otherBlockSize);

			auto thisTypeAndSize = setTypeSize(DataBlock, end - readSoFar);
			auto nextBlock		 = i < blocks - 1 ? addresses[i + 1] : storage->empty();
			bytes numbers;
			if (i == 0)
			{
				numbers = concatNumbers(4, thisTypeAndSize, nextBlock, next, key);
			}
			else
			{
				numbers = concatNumbers(2, thisTypeAndSize, nextBlock);
			}

			storage->set(
				addresses[i],
				concat(2, &numbers, &buffer));

			readSoFar = end;
		}

		return addresses[0];
	}

	tuple<bytes, number, number> Tree::readDataBlock(bytes block)
	{
		bytes data;
		auto address = storage->empty();
		number nextBucket;
		number key;

		while (true)
		{
			auto first = address == storage->empty();

			auto read = first ? block : storage->get(address);

			auto deconstructed = deconstruct(read, {(first ? 4 : 2) * (int)sizeof(number)});
			auto numbers	   = deconstructNumbers(deconstructed[0]);
			auto blockData	 = deconstructed[1];

			auto [type, thisSize] = getTypeSize(numbers[0]);
			if (type != DataBlock)
			{
				throw Exception("attempt to read a non-data block as data block");
			}
			auto nextBlock = numbers[1];

			if (first)
			{
				nextBucket = numbers[2];
				key		   = numbers[3];
			}
			blockData.resize(thisSize);
			data = concat(2, &data, &blockData);

			if (nextBlock != storage->empty())
			{
				address = nextBlock;
				continue;
			}
			else
			{
				return {data, key, nextBucket};
			}
		}
	}

	pair<BlockType, bytes> Tree::checkType(number address)
	{
		auto block		   = storage->get(address);
		auto deconstructed = deconstruct(block, {sizeof(number)});
		auto typeAndSize   = numberFromBytes(deconstructed[0]);
		return {getTypeSize(typeAndSize).first, block};
	}

	void Tree::checkConsistency()
	{
		checkConsistency(root, ULONG_MAX, true);
	}

	void Tree::checkConsistency(number address, number largestKey, bool rightmost)
	{
		auto throwIf = [](bool expression, Exception exception) {
			if (expression)
			{
				throw exception;
			}
		};

		auto [type, read] = checkType(address);
		switch (type)
		{
			case NodeBlock:
			{
				auto block = readNodeBlock(read);
				throwIf(
					(block.size() < b / 2 && !rightmost) || block.size() == 0,
					Exception(boost::format("block undeflow (%1%) for b = %2% and block is not the rightmost") % block.size() % b));

				for (unsigned int i = 0; i < block.size(); i++)
				{
					if (i != 0)
					{
						throwIf(
							block[i].first < block[i - 1].first,
							Exception("wrong order of keys in a block"));
					}
					throwIf(
						block[i].first > largestKey,
						Exception("keys larger than the parent key is found in a block"));
					throwIf(
						block[i].second == storage->empty(),
						Exception("empty pointer found in a block"));

					checkConsistency(block[0].second, block[0].first, rightmost && i == block.size() - 1);
				}
				break;
			}
			case DataBlock:
			{
				auto block = readDataBlock(read);
				throwIf(
					get<2>(block) == storage->empty() && !rightmost,
					Exception("empty pointer to the next data block, not the rightmost"));
				throwIf(
					get<1>(block) != largestKey,
					Exception("data block has the key different from the parent's"));
				break;
			}
			default:
				throw Exception(boost::format("invalid block type: %1%") % type);
		}
	}

	pair<BlockType, number> getTypeSize(number typeAndSize)
	{
		number buffer[1]{typeAndSize};
		auto type = ((unsigned int *)buffer)[0];
		auto size = ((unsigned int *)buffer)[1];

		return {(BlockType)type, (number)size};
	}

	number setTypeSize(BlockType type, number size)
	{
		unsigned int buffer[2]{type, (unsigned int)size};
		return ((number *)buffer)[0];
	}
}
