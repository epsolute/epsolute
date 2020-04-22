#pragma once

#include "definitions.h"

#include <string>

namespace DPORAM
{
	using namespace std;

	vector<pair<number, number>> BRC(number fanout, number height, number from, number to);
}
