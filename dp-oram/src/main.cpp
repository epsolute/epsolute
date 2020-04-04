#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <iostream>

using namespace std;
using namespace BPlusTree;
using namespace PathORAM;

int main()
{
	cout << "constructing data set" << endl;

	const auto COUNT			 = 500;
	const auto ORAM_BLOCK_SIZE	 = 256;
	const auto ORAM_LOG_CAPACITY = 10;
	const auto ORAM_Z			 = 3;
	const auto TREE_BLOCK_SIZE	 = 64;

	vector<pair<number, bytes>> data;
	for (number i = 0; i < COUNT; i++)
	{
		data.push_back({i, PathORAM::fromText(to_string(i), ORAM_BLOCK_SIZE)});
	}

	auto oram = new ORAM(ORAM_LOG_CAPACITY, ORAM_BLOCK_SIZE, ORAM_Z); // TODO use FS

	for (auto record : data)
	{
		oram->put(record.first, record.second);
	}

	vector<pair<number, bytes>> index;
	for (auto record : data)
	{
		index.push_back({record.first, BPlusTree::bytesFromNumber(record.first)});
	}

	auto treeStorage = new BPlusTree::InMemoryStorageAdapter(TREE_BLOCK_SIZE);
	auto tree		 = new Tree(treeStorage, index);

	// query
	const auto QUERY = 50;
	auto oramId		 = BPlusTree::numberFromBytes(tree->search(QUERY)[0]);
	auto block		 = oram->get(oramId);
	auto result		 = PathORAM::toText(block, ORAM_BLOCK_SIZE);

	cout << "For query " << QUERY << " the result is " << result << endl;

	delete oram;
	delete treeStorage;
	delete tree;

	return 0;
}
