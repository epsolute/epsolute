#include "utility.hpp"

#include "path-oram/utility.hpp"

#include <boost/random/laplace_distribution.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/variate_generator.hpp>
#include <math.h>

namespace DPORAM
{
	using namespace std;

	number gammaNodes(number m, double beta, number kZero)
	{
		auto gamma = sqrt(-3 * (long)m * log(beta) / kZero);
		return (number)ceil((1 + gamma) * kZero / (long)m);
	}

	tuple<number, number, number, number> padToBuckets(pair<number, number> query, number min, number max, number buckets)
	{
		auto step = (double)(max - min) / buckets;

		auto fromBucket = (number)floor(((query.first - min) / step));
		auto toBucket	= (number)floor(((query.second - min) / step));

		fromBucket = fromBucket == buckets ? fromBucket - 1 : fromBucket;
		toBucket   = toBucket == buckets ? toBucket - 1 : toBucket;

		return {fromBucket, toBucket, fromBucket * step + min, (toBucket + 1) * step + min};
	}

	number optimalMu(double beta, number k, number N, number epsilon, number levels, number orams)
	{
		auto nodes	 = 0uLL;
		auto atLevel = (number)(log(N) / log(k));
		for (auto level = 0uLL; level <= levels; level++)
		{
			nodes += atLevel;
			atLevel /= k;
		}
		nodes *= orams;

		auto mu = (number)ceil(-(double)levels * log(2 - 2 * pow(1 - beta, 1.0 / nodes)) / epsilon);

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

	vector<pair<number, number>> BRC(number fanout, number from, number to, number maxLevel)
	{
		vector<pair<number, number>> result;
		number level = 0; // leaf-level, bottom

		do
		{
			// move FROM to the right within the closest parent, but no more than TO;
			// exit if FROM hits a boundary AND level is not maximal;
			while ((from % fanout != 0 || level == maxLevel) && from < to)
			{
				result.push_back({level, from});
				from++;
			}

			// move TO to the left within the closest parent, but no more than FROM;
			// exit if TO hits a boundary AND level is not maximal
			while ((to % fanout != fanout - 1 || level == maxLevel) && from < to)
			{
				result.push_back({level, to});
				to--;
			}

			// after we moved FROM and TO towards each other they may or may not meet
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
