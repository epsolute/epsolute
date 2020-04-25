#include "definitions.h"
#include "utility.hpp"

#include "gtest/gtest.h"

using namespace std;

namespace DPORAM
{
	class UtilityLaplaceTest : public testing::TestWithParam<pair<double, double>>
	{
	};

	TEST_P(UtilityLaplaceTest, LaplaceCDFCheck)
	{
		const auto RUNS = 10000;
		const auto step = 0.1;
		auto [mu, b]	= GetParam();

		auto min = mu - 5.0, max = mu + 5.0;

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

	string printTestName(testing::TestParamInfo<pair<double, double>> input)
	{
		auto [mu, beta] = input.param;
		return boost::str(boost::format("mu%1%beta%2%") % (mu >= 0 ? to_string((int)mu) : "MINUS" + to_string(-(int)mu)) % beta);
	}

	vector<pair<double, double>> cases = {
		{5.0, 1.0},
		{5.0, 2.0},
		{5.0, 3.0},
		{5.0, 4.0},
		{0.0, 1.0},
		{-5.0, 1.0}};

	INSTANTIATE_TEST_SUITE_P(UtilityLaplaceSuite, UtilityLaplaceTest, testing::ValuesIn(cases), printTestName);
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
