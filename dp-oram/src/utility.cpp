#include "utility.hpp"

#include "path-oram/utility.hpp"

#include <boost/random/laplace_distribution.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/variate_generator.hpp>
#include <math.h>

namespace DPORAM
{
	using namespace std;

	string exec(string cmd)
	{
		array<char, 128> buffer;
		string result;
		unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
		if (!pipe)
		{
			throw runtime_error("popen() failed!");
		}
		while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
		{
			result += buffer.data();
		}
		return result;
	}

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

	wstring timeToString(long long time)
	{
		wstringstream text;
		vector<wstring> units = {
			L"ns",
			L"μs",
			L"ms",
			L"s"};
		for (number i = 0; i < units.size(); i++)
		{
			if (time < 10000 || i == units.size() - 1)
			{
				text << time << L" " << units[i];
				break;
			}
			else
			{
				time /= 1000;
			}
		}

		return text.str();
	}

	wstring bytesToString(long long bytes)
	{
		wstringstream text;
		vector<wstring> units = {
			L"B",
			L"KB",
			L"MB",
			L"GB"};
		for (number i = 0; i < units.size(); i++)
		{
			if (bytes < (1 << 13) || i == units.size() - 1)
			{
				text << bytes << L" " << units[i];
				break;
			}
			else
			{
				bytes >>= 10;
			}
		}

		return text.str();
	}

	number salaryToNumber(string salary)
	{
		auto salaryDouble = stod(salary) * 100;
		auto salaryNumber = (long long)salaryDouble + OFFSET;
		return (number)salaryNumber;
	}

	double numberToSalary(number salary)
	{
		return ((long long)salary - OFFSET) * 0.01;
	}

	string redishost(string host, int i)
	{
		return host + (i > -1 ? ("/" + to_string(i)) : "");
	}
}
