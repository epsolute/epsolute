#pragma once

#include "definitions.h"
#include "storage-adapter.hpp"

namespace BPlusTree
{
	using namespace std;

	class Tree
	{
		public:
		number search(number key);

		Tree(AbsStorageAdapter *storage);
		Tree(AbsStorageAdapter *storage, vector<pair<number, bytes>> data);

		private:
		AbsStorageAdapter *storage;
		number root;
		number b;
		number leftmostDataBlock; // for testing

		number createDataBlock(bytes data, number next);
		pair<bytes, number> readDataBlock(number address);

		friend class TreeTest_ReadDataLayer_Test;
	};
}
