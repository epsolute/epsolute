#include "utility.hpp"

#include "path-oram/utility.hpp"

#include <boost/random/laplace_distribution.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/variate_generator.hpp>

namespace DPORAM
{
	using namespace std;

	tuple<number, number, number, number> padToBuckets(pair<number, number> query, number min, number max, number buckets)
	{
		auto domain = max - min;

		auto fromBucket = (number)floor((query.first - min) * (double)(buckets) / domain);
		auto toBucket	= (number)ceil((query.second - min) * (double)(buckets) / domain);

		// there is a special case when query is {min, min};
		// this would result in equal from/to buckets, so we adjust
		if (query.first == query.second && query.first == min)
		{
			toBucket++;
		}

		auto scale = domain / (double)buckets;

		return {fromBucket, toBucket, fromBucket * scale + min, toBucket * scale + min};
	}

	number optimalMu(double beta, number k, number N, number epsilon)
	{
		auto nodesExp = ceil(log(k - 1) / log(k) + log(N) / log(k) - 1);
		auto nodes	  = (pow(k, nodesExp) - 1) / (k - 1) + N;

		auto mu = (number)ceil(-log(N) / (log(k) * epsilon) * log(2 - 2 * pow(1 - beta, 1 / nodes)));

		return mu;
	}

	double sampleLaplace(double mu, double b)
	{
		auto seed = PathORAM::getRandomULong(ULONG_MAX);
		boost::minstd_rand generator(seed);

		auto laplaceDistribution = boost::random::laplace_distribution(mu, b);
		boost::variate_generator variateGenerator(generator, laplaceDistribution);

		return variateGenerator();
	}

	vector<pair<number, number>> BRC(number fanout, number from, number to)
	{
		vector<pair<number, number>> result;
		int level = 0; // leaf-level, bottom

		do
		{
			// move FROM to the right withing the closest parent, but no more than TO
			while (from % fanout != 0 && from < to)
			{
				result.push_back({level, from});
				from++;
			}

			// move TO to the left withing the closest parent, but no more than FROM
			while (to % fanout != fanout - 1 && from < to)
			{
				result.push_back({level, to});
				to--;
			}

			// after we moved FROM and TO towrds each other they may or may not meet
			if (from != to)
			{
				// if they have not met, we climb one more level
				from /= fanout;
				to /= fanout;

				level++;
			}
			else
			{
				// otherwise we added this last node to which both FROM and TO are pointing, and return
				result.push_back({level, from});
				return result;
			}
		} while (true);
	}
}
