#pragma once

#include "definitions.h"

#include <string>

namespace DPORAM
{
	using namespace std;

	vector<pair<number, number>> BRC(number fanout, number from, number to);

	double sampleLaplace(double mu, double b);
}
