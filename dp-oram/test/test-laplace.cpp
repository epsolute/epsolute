#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace DPORAM
{
	class UtilityLaplaceTest : public ::testing::Test
	{
	};

	TEST_F(UtilityLaplaceTest, LaplacePRFCheck)
	{
		const auto RUNS = 10000;
		const auto min = 0.0, max = 10.0, step = 0.1, mu = 5.0, b = 1.0;

		auto cdf = [](double mu, double b, double x) -> double {
			return x <= mu ?
					   0.5 * exp((x - mu) / b) :
					   1 - 0.5 * exp(-(x - mu) / b);
		};

		vector<double> samples;
		samples.reserve(RUNS);
		map<double, double> cdfActual;

		for (auto i = 0; i < RUNS; i++)
		{
			auto sampled = sampleLaplace(mu, b);
			samples.push_back(sampled);
		}

		sort(samples.begin(), samples.end());

		auto value = min;
		for (auto i = 0; i < RUNS; i++)
		{
			if (samples[i] > value)
			{
				cdfActual[value] = (double)i / RUNS;
				value += step;
				i--;
			}
		}

		for (auto i = min; i < max; i += step)
		{
			auto actual	  = cdfActual[i];
			auto expected = cdf(mu, b, i);

			EXPECT_NEAR(expected, actual, 0.05);
		}
	}
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
