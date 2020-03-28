#pragma once

#include "definitions.h"

#include <string>

namespace BPlusTree
{
	using namespace std;

	bytes fromText(string text, number BLOCK_SIZE);
	string toText(bytes data, number BLOCK_SIZE);
}
