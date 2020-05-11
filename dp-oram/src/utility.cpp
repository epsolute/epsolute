#include "utility.hpp"

#include "path-oram/utility.hpp"

#include <boost/random/laplace_distribution.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/variate_generator.hpp>
#include <math.h>

namespace DPORAM
{
	using namespace std;

	tuple<number, number, number, number> padToBuckets(pair<number, number> query, number min, number max, number buckets)
	{
		auto step = (double)(max - min) / buckets;

		auto fromBucket = (number)floor(((query.first - min) / step));
		auto toBucket	= (number)floor(((query.second - min) / step));

		fromBucket = fromBucket == buckets ? fromBucket - 1 : fromBucket;
		toBucket   = toBucket == buckets ? toBucket - 1 : toBucket;

		return {fromBucket, toBucket, fromBucket * step + min, (toBucket + 1) * step + min};
	}

	number optimalMu(double beta, number k, number N, number epsilon, number orams)
	{
		auto nodesExp = ceil(log(k - 1) / log(k) + log(N) / log(k) - 1);
		auto nodes	  = (pow(k, nodesExp) - 1) / (k - 1) + N;
		nodes *= orams;

		auto mu = (number)ceil(-log(N) / (log(k) * epsilon) * log(2 - 2 * pow(1 - beta, 1 / nodes)));

		return mu;
	}

	double sampleLaplace(double mu, double lambda)
	{
		auto seed = PathORAM::getRandomULong(ULONG_MAX);
		boost::minstd_rand generator(seed);

		auto laplaceDistribution = boost::random::laplace_distribution(mu, lambda);
		boost::variate_generator variateGenerator(generator, laplaceDistribution);

		return variateGenerator();
	}

	vector<pair<number, number>> BRC(number fanout, number from, number to)
	{
		vector<pair<number, number>> result;
		int level = 0; // leaf-level, bottom

		do
		{
			// move FROM to the right within the closest parent, but no more than TO
			while (from % fanout != 0 && from < to)
			{
				result.push_back({level, from});
				from++;
			}

			// move TO to the left within the closest parent, but no more than FROM
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
