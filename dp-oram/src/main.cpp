#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <iostream>

using namespace std;
using namespace BPlusTree;
using namespace PathORAM;

bytes random(int size);
int tree();
int oram();

int main()
{
	tree();
	oram();

	return 0;
}

int tree()
{
	cout << "Running random small simulation using compiled shared library..." << endl;

	const auto BLOCK_SIZE = 64;
	const auto COUNT	  = 100000;
	const auto QUERIES	  = 1000;
	const auto RANGE	  = 15;

	cout << "BLOCK_SIZE: " << BLOCK_SIZE << endl;
	cout << "COUNT: " << COUNT << endl;
	cout << "QUERIES: " << QUERIES << endl;
	cout << "RANGE: " << RANGE << endl;

	vector<pair<number, bytes>> data;
	for (number i = 0; i < COUNT; i++)
	{
		data.push_back({i, random(BLOCK_SIZE - 4 * sizeof(number))});
	}

	auto storage = new BPlusTree::InMemoryStorageAdapter(BLOCK_SIZE);
	auto tree	 = new Tree(storage, data);

	for (number i = 0; i < QUERIES; i++)
	{
		number start = rand() % (COUNT - RANGE);
		tree->search(start, start + RANGE - 1);
	}

	delete storage;
	delete tree;

	cout << "Successful!" << endl;

	return 0;
}

int oram()
{
	cout << "Running random small simulation using compiled shared library..." << endl;

	const auto LOG_CAPACITY = 5;
	const auto BLOCK_SIZE	= 32;
	const auto Z			= 3;
	const auto CAPACITY		= (1 << LOG_CAPACITY) * Z;
	const auto ELEMENTS		= (CAPACITY / 4) * 3;

	cout << "LOG_CAPACITY: " << LOG_CAPACITY << endl;
	cout << "BLOCK_SIZE: " << BLOCK_SIZE << endl;
	cout << "Z: " << Z << endl;
	cout << "CAPACITY: " << CAPACITY << endl;
	cout << "ELEMENTS: " << ELEMENTS << endl;

	auto oram = new ORAM(LOG_CAPACITY, BLOCK_SIZE, Z);

	// put all
	for (number id = 0; id < ELEMENTS; id++)
	{
		auto data = PathORAM::fromText(to_string(id), BLOCK_SIZE);
		oram->put(id, data);
	}

	// get all
	for (number id = 0; id < ELEMENTS; id++)
	{
		oram->get(id);
	}

	// random operations
	for (number i = 0; i < 2 * ELEMENTS; i++)
	{
		auto id	  = getRandomULong(ELEMENTS);
		auto read = getRandomULong(2) == 0;
		if (read)
		{
			// get
			oram->get(id);
		}
		else
		{
			auto data = PathORAM::fromText(to_string(ELEMENTS + getRandomULong(ELEMENTS)), BLOCK_SIZE);
			oram->put(id, data);
		}
	}

	delete oram;

	cout << "Successful!" << endl;

	return 0;
}

bytes random(int size)
{
	uchar material[size];
	for (int i = 0; i < size; i++)
	{
		material[i] = (uchar)rand();
	}
	return bytes(material, material + size);
}
