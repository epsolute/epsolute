#include "tree.hpp"
#include "utility.hpp"

#include <iostream>

using namespace std;
using namespace BPlusTree;

bytes random(int size);

int main()
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

	auto storage = new InMemoryStorageAdapter(BLOCK_SIZE);
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

bytes random(int size)
{
	uchar material[size];
	for (int i = 0; i < size; i++)
	{
		material[i] = (uchar)rand();
	}
	return bytes(material, material + size);
}
