#pragma once

#include "definitions.h"
#include "storage-adapter.hpp"

namespace BPlusTree
{
	using namespace std;

	enum BlockType
	{
		DataBlock,
		NodeBlock
	};

	class Tree
	{
		public:
		vector<bytes> search(number key);
		vector<bytes> search(number start, number end);

		Tree(AbsStorageAdapter *storage);
		Tree(AbsStorageAdapter *storage, vector<pair<number, bytes>> data);

		private:
		AbsStorageAdapter *storage;
		number root;
		number b;

		number leftmostDataBlock; // for testing

		number createDataBlock(bytes data, number key, number next);
		tuple<bytes, number, number> readDataBlock(bytes block);

		number createNodeBlock(vector<pair<number, number>> data);
		vector<pair<number, number>> readNodeBlock(bytes block);

		pair<BlockType, bytes> checkType(number address);

		vector<pair<number, number>> pushLayer(vector<pair<number, number>> input);

		void checkConsistency(number address, number largestKey, bool rightmost);
		void checkConsistency();

		friend class TreeTest_ReadDataLayer_Test;
		friend class TreeTest_CreateNodeBlockTooBig_Test;
		friend class TreeTest_CreateNodeBlock_Test;
		friend class TreeTest_ReadNodeBlock_Test;
		friend class TreeTest_PushLayer_Test;
		friend class TreeTest_ConsistencyCheck_Test;
		friend class TreeTest_ConsistencyCheckWrongBlockType_Test;
		friend class TreeTest_ConsistencyCheckDataBlockPointer_Test;
		friend class TreeTest_ConsistencyCheckDataBlockKey_Test;
		friend class TreeTestBig_Simulation_Test;
	};
}
