#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <iostream>

using namespace std;
using namespace DPORAM;

int main()
{
	cout << "constructing data set" << endl;

	const auto OVERRIDE = true;

	const auto COUNT			 = 1000uLL;
	const auto ORAM_BLOCK_SIZE	 = 256uLL;
	const auto ORAM_LOG_CAPACITY = 10uLL;
	const auto ORAM_Z			 = 3uLL;
	const auto TREE_BLOCK_SIZE	 = 64uLL;

	vector<pair<number, bytes>> data;
	for (number i = 0; i < COUNT; i++)
	{
		data.push_back({i, PathORAM::fromText(to_string(i), ORAM_BLOCK_SIZE)});
	}

	auto oramKey = bytes();
	oramKey.resize(32);

	auto oramStorage	 = new PathORAM::FileSystemStorageAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, "oram.bin", OVERRIDE);
	auto oramPositionMap = new PathORAM::InMemoryPositionMapAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z);
	auto oramStash		 = new PathORAM::InMemoryStashAdapter(3 * ORAM_LOG_CAPACITY * ORAM_Z);
	auto oram			 = new PathORAM::ORAM(
		   ORAM_LOG_CAPACITY,
		   ORAM_BLOCK_SIZE,
		   ORAM_Z,
		   oramStorage,
		   oramPositionMap,
		   oramStash);

	for (auto record : data)
	{
		oram->put(record.first, record.second);
	}

	vector<pair<number, bytes>> index;
	for (auto record : data)
	{
		index.push_back({record.first, BPlusTree::bytesFromNumber(record.first)});
	}

	auto treeStorage = new BPlusTree::FileSystemStorageAdapter(TREE_BLOCK_SIZE, "tree.bin", OVERRIDE);
	auto tree		 = OVERRIDE ? new BPlusTree::Tree(treeStorage, index) : new BPlusTree::Tree(treeStorage);

	// query
	const auto QUERY = 5;
	auto oramId		 = BPlusTree::numberFromBytes(tree->search(QUERY)[0]);
	auto block		 = oram->get(oramId);
	auto result		 = PathORAM::toText(block, ORAM_BLOCK_SIZE);

	cout << "For query " << QUERY << " the result is " << result << endl;

	delete oramStorage;
	delete oramPositionMap;
	delete oramStash;
	delete oram;

	delete treeStorage;
	delete tree;

	return 0;
}
