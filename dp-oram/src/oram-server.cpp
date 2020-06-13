#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"
#include "utility.hpp"

#include <boost/program_options.hpp>
#include <future>
#include <iomanip>
#include <iostream>
#include <rpc/server.h>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;

auto ORAM_BLOCK_SIZE	   = 256uLL;
number PORT				   = RPC_PORT;
auto USE_ORAM_OPTIMIZATION = true;

mutex orams_mutex;
vector<pair<number, shared_ptr<PathORAM::ORAM>>> orams;

// returns tuple<real records, thread overhead, # of processed requests>
using queryReturnType = tuple<vector<bytes>, chrono::steady_clock::rep, number>;

void setOram(number oramNumber, string redisHost, vector<pair<number, bytes>> indices, number logCapacity, number blockSize, number z);
vector<queryReturnType> runQuery(vector<pair<number, vector<number>>> blockIds, pair<number, number> query);
void reset();

int main(int argc, char* argv[])
{
	po::options_description desc("Redis overhead macro benchmark", 120);
	desc.add_options()("help,h", "produce help message");
	desc.add_options()("port", po::value<number>(&PORT)->default_value(PORT), "Port to bind to");
	desc.add_options()("useOramOptimization", po::value<bool>(&USE_ORAM_OPTIMIZATION)->default_value(USE_ORAM_OPTIMIZATION), "if set will use ORAM batch processing");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << "\n";
		exit(1);
	}

	cout << "main: optimize=" << USE_ORAM_OPTIMIZATION << endl;

	rpc::server srv(PORT);
	srv.bind("setOram", &setOram);
	srv.bind("runQuery", &runQuery);
	srv.bind("reset", &reset);
	srv.run();

	return 0;
}

void reset()
{
	orams.clear();
	cout << "reset: done" << endl;
}

vector<queryReturnType> runQuery(vector<pair<number, vector<number>>> blockIds, pair<number, number> query)
{
	cout << "runQuery: " << blockIds.size() << " sets, query={" << numberToSalary(query.first) << ", " << numberToSalary(query.second) << "}" << endl;

	auto queryOram = [](const vector<number>& ids, shared_ptr<PathORAM::ORAM> oram, number from, number to, promise<queryReturnType>* promise) -> void {
		vector<bytes> answer;
		vector<bytes> realRecords;

		auto start = chrono::steady_clock::now();

		if (ids.size() > 0)
		{
			if (USE_ORAM_OPTIMIZATION)
			{
				answer.reserve(ids.size());
				vector<pair<number, bytes>> requests;
				requests.resize(ids.size());

				transform(ids.begin(), ids.end(), requests.begin(), [](number id) { return make_pair(id, bytes()); });
				oram->multiple(requests, answer);
			}
			else
			{
				for (auto&& id : ids)
				{
					bytes record;
					oram->get(id, record);
					auto text = PathORAM::toText(record, ORAM_BLOCK_SIZE);

					vector<string> broken;
					boost::algorithm::split(broken, text, boost::is_any_of(","));
					auto salary = salaryToNumber(broken[7]);

					if (salary >= from && salary <= to)
					{
						realRecords.push_back(record);
					}
				}
			}

			if (USE_ORAM_OPTIMIZATION)
			{
				for (auto&& record : answer)
				{
					auto text = PathORAM::toText(record, ORAM_BLOCK_SIZE);

					vector<string> broken;
					boost::algorithm::split(broken, text, boost::is_any_of(","));
					auto salary = salaryToNumber(broken[7]);

					if (salary >= from && salary <= to)
					{
						realRecords.push_back(record);
					}
				}
			}
		}

		auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();

		promise->set_value({realRecords, elapsed, ids.size()});
	};

	thread threads[orams.size()];
	promise<queryReturnType> promises[orams.size()];
	future<queryReturnType> futures[orams.size()];
	vector<queryReturnType> result;
	result.reserve(orams.size());

	// here we do not care in which order the results will arrive,
	// but we do care that oram ID matches blockIdsSet ID
	for (auto i = 0uLL; i < orams.size(); i++)
	{
		futures[i] = promises[i].get_future();
		for (auto&& blockIdsSet : blockIds)
		{
			if (blockIdsSet.first == orams[i].first)
			{
				threads[i] = thread(queryOram, blockIdsSet.second, orams[i].second, query.first, query.second, &promises[i]);
				break;
			}
		}
	}

	for (auto i = 0uLL; i < orams.size(); i++)
	{
		result.push_back(futures[i].get());
		threads[i].join();
	}

	cout << "runQuery: done" << endl;

	return result;
}

void setOram(number oramNumber, string redisHost, vector<pair<number, bytes>> indices, number logCapacity, number blockSize, number z)
{
	cout << "setOram: ID " << oramNumber << ", indices length " << indices.size() << ", logCapacity=" << logCapacity << ", blockSize=" << blockSize << ", z=" << z << endl;

	auto oram = make_shared<PathORAM::ORAM>(
		logCapacity,
		blockSize,
		z,
		make_shared<PathORAM::RedisStorageAdapter>((1 << logCapacity) + z, ORAM_BLOCK_SIZE, PathORAM::getRandomBlock(KEYSIZE), redishost(redisHost, oramNumber), true, z),
		make_shared<PathORAM::InMemoryPositionMapAdapter>(((1 << logCapacity) * z) + z),
		make_shared<PathORAM::InMemoryStashAdapter>(3 * logCapacity * z),
		true,
		ULONG_MAX);

	oram->load(indices);

	lock_guard<mutex> guard(orams_mutex);

	orams.push_back({oramNumber, oram});

	cout << "setOram: done" << endl;
}
