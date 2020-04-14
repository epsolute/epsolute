#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;
namespace pt = boost::property_tree;

po::variables_map setupArgs(int argc, char* argv[]);
vector<pair<number, number>> query(vector<shared_ptr<PathORAM::ORAM>> orams, shared_ptr<BPlusTree::Tree> tree, vector<pair<number, number>> queries);
tuple<vector<pair<number, bytes>>, vector<pair<number, bytes>>, vector<pair<number, number>>> generateData(bool read);
tuple<vector<ORAMSet>, shared_ptr<BPlusTree::AbsStorageAdapter>, shared_ptr<BPlusTree::Tree>> constructIndices(vector<pair<number, bytes>> oramIndex, vector<pair<number, bytes>> treeIndex, bool generate);

void writeJson(vector<pair<number, number>> measurements);

number salaryToNumber(string salary);
double numberToSalary(number salary);
string filename(string filename, int i);
string redishost(string host, int i);

void LOG(LOG_LEVEL level, string message);
void LOG(LOG_LEVEL level, boost::format message);

auto COUNT				   = 1000uLL;
auto ORAM_BLOCK_SIZE	   = 256uLL;
auto ORAM_LOG_CAPACITY	   = 10uLL;
auto ORAMS_NUMBER		   = 1;
auto PARALLEL			   = true;
const auto ORAM_Z		   = 3uLL;
const auto TREE_BLOCK_SIZE = 64uLL;
auto ORAM_STORAGE		   = FileSystem;

const auto KEY_FILE			 = "key";
const auto TREE_FILE		 = "tree";
const auto ORAM_STORAGE_FILE = "oram-storage";
const auto ORAM_MAP_FILE	 = "oram-map";
const auto ORAM_STASH_FILE	 = "oram-stash";

const auto ORAM_REDIS_HOST	   = "tcp://127.0.0.1:6379";
const auto ORAM_AEROSPIKE_HOST = "127.0.0.1";

const auto DATA_FILE  = "../../experiments-scripts/scripts/data.csv";
const auto QUERY_FILE = "../../experiments-scripts/scripts/query.csv";

int main(int argc, char* argv[])
{
	auto vm = setupArgs(argc, argv);

	if (!chrono::high_resolution_clock::is_steady)
	{
		LOG(WARNING, "high_resolution_clock is not steady in this system");
	}

	auto [oramIndex, treeIndex, queries] = generateData(vm["readInputs"].as<bool>());

	COUNT = oramIndex.size();

	vector<int> sizes;
	sizes.resize(COUNT);
	transform(oramIndex.begin(), oramIndex.end(), sizes.begin(), [](pair<number, bytes> val) { return val.second.size(); });
	ORAM_BLOCK_SIZE = *max_element(sizes.begin(), sizes.end());

	ORAM_LOG_CAPACITY = ceil(log2(COUNT / ORAMS_NUMBER)) + 1;

	LOG(INFO, boost::format("COUNT = %1%") % COUNT);
	LOG(INFO, boost::format("ORAM_BLOCK_SIZE = %1%") % ORAM_BLOCK_SIZE);
	LOG(INFO, boost::format("ORAM_LOG_CAPACITY = %1%") % ORAM_LOG_CAPACITY);
	LOG(INFO, boost::format("ORAMS_NUMBER = %1%") % ORAMS_NUMBER);
	LOG(INFO, boost::format("PARALLEL = %1%") % PARALLEL);
	LOG(INFO, boost::format("ORAM_Z = %1%") % ORAM_Z);
	LOG(INFO, boost::format("TREE_BLOCK_SIZE = %1%") % TREE_BLOCK_SIZE);
	LOG(INFO, boost::format("ORAM_BACKEND = %1%") % oramBackendStrings[ORAM_STORAGE]);

	auto [oramSets, treeStorage, tree] = constructIndices(oramIndex, treeIndex, vm["generateIndices"].as<bool>());

	vector<shared_ptr<PathORAM::ORAM>> orams;
	orams.resize(oramSets.size());
	transform(oramSets.begin(), oramSets.end(), orams.begin(), [](ORAMSet val) { return get<3>(val); });
	auto measurements = query(orams, tree, queries);

	LOG(INFO, "Complete!");

	vector<number> overheads;
	overheads.resize(measurements.size());
	transform(measurements.begin(), measurements.end(), overheads.begin(), [](pair<number, number> val) { return val.first; });
	auto sum	 = accumulate(overheads.begin(), overheads.end(), 0.0);
	auto average = sum / overheads.size();

	LOG(INFO, boost::format("Total: %1% ms, average: %2% μs per query") % (sum / 1000 / 1000) % (average / 1000));

	writeJson(measurements);

	return 0;
}

po::variables_map setupArgs(int argc, char* argv[])
{
	po::options_description desc("range query processor");
	desc.add_options()("help", "produce help message");
	desc.add_options()("generateIndices", po::value<bool>()->default_value(true), "if set, will generate ORAM and tree indices, otherwise will read files");
	desc.add_options()("readInputs", po::value<bool>()->default_value(true), "if set, will read inputs from files");
	desc.add_options()("parallel", po::value<bool>(&PARALLEL)->default_value(true), "if set, will query orams in parallel");
	desc.add_options()("oramStorage", po::value<ORAM_BACKEND>(&ORAM_STORAGE)->default_value(FileSystem), "the ORAM backend to use");
	desc.add_options()("oramsNumber", po::value<int>(&ORAMS_NUMBER)->notifier([](int v) { if (v < 1 || v > 16) { throw Exception("malformed --oramsNumber"); } })->default_value(1), "the number of parallel ORAMs to use");
	desc.add_options()("verbosity", po::value<LOG_LEVEL>(&__logLevel)->default_value(INFO), "verbosity level to output");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << "\n";
		exit(1);
	}

	return vm;
}

vector<pair<number, number>> query(vector<shared_ptr<PathORAM::ORAM>> orams, shared_ptr<BPlusTree::Tree> tree, vector<pair<number, number>> queries)
{
	LOG(INFO, boost::format("Running %1% queries...") % queries.size());

	vector<pair<number, number>> measurements;

	auto queryOram = [](vector<number> ids, shared_ptr<PathORAM::ORAM> oram, promise<vector<bytes>>* promise) -> vector<bytes> {
		vector<bytes> answer;
		for (auto id : ids)
		{
			auto block = oram->get(id);
			answer.push_back(block);
		}

		if (promise != NULL)
		{
			promise->set_value(answer);
		}

		return answer;
	};

	for (auto query : queries)
	{
		auto start = chrono::high_resolution_clock::now();

		auto oramIds = tree->search(query.first, query.second);
		vector<vector<number>> blockIds;
		blockIds.resize(ORAMS_NUMBER);
		for (auto oramId : oramIds)
		{
			auto blockId = BPlusTree::numberFromBytes(oramId);
			blockIds[blockId % ORAMS_NUMBER].push_back(blockId / ORAMS_NUMBER);
		}

		auto count = 0;
		if (PARALLEL)
		{
			thread threads[ORAMS_NUMBER];
			promise<vector<bytes>> promises[ORAMS_NUMBER];
			future<vector<bytes>> futures[ORAMS_NUMBER];

			for (auto i = 0; i < ORAMS_NUMBER; i++)
			{
				futures[i] = promises[i].get_future();
				threads[i] = thread(queryOram, blockIds[i], orams[i], &promises[i]);
			}

			for (auto i = 0; i < ORAMS_NUMBER; i++)
			{
				auto result = futures[i].get();
				threads[i].join();
				count += result.size();
			}
		}
		else
		{
			for (auto i = 0; i < ORAMS_NUMBER; i++)
			{
				auto result = queryOram(blockIds[i], orams[i], NULL);
				count += result.size();
			}
		}

		auto end	 = chrono::high_resolution_clock::now();
		auto elapsed = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
		measurements.push_back({elapsed, count});

		LOG(TRACE, boost::format("For query {%9.2f, %9.2f} the result size is %3i (completed in %6i μs, or %4i μs per record)") % numberToSalary(query.first) % numberToSalary(query.second) % count % (elapsed / 1000) % (count > 0 ? (elapsed / 1000 / count) : 0));
	}

	return measurements;
}

tuple<vector<pair<number, bytes>>, vector<pair<number, bytes>>, vector<pair<number, number>>> generateData(bool read)
{
	LOG(INFO, "Constructing data set...");

	vector<pair<number, bytes>> oramIndex;
	vector<pair<number, bytes>> treeIndex;
	vector<pair<number, number>> queries;
	if (read)
	{
		ifstream dataFile(DATA_FILE);

		string line = "";
		auto i		= 0;
		while (getline(dataFile, line))
		{
			vector<string> record;
			boost::algorithm::split(record, line, boost::is_any_of(","));
			auto salary = salaryToNumber(record[7]);

			LOG(TRACE, boost::format("Salary: %9.2f, data length: %3i") % numberToSalary(salary) % line.size());

			oramIndex.push_back({i, PathORAM::fromText(line, ORAM_BLOCK_SIZE)});
			treeIndex.push_back({salary, BPlusTree::bytesFromNumber(i)});
			i++;
		}
		dataFile.close();

		ifstream queryFile(QUERY_FILE);

		line = "";
		while (getline(queryFile, line))
		{
			vector<string> query;
			boost::algorithm::split(query, line, boost::is_any_of(","));
			auto left  = salaryToNumber(query[0]);
			auto right = salaryToNumber(query[1]);

			LOG(TRACE, boost::format("Query: {%9.2f, %9.2f}") % numberToSalary(left) % numberToSalary(right));

			queries.push_back({left, right});
		}
		queryFile.close();
	}
	else
	{
		for (number i = 0; i < COUNT; i++)
		{
			oramIndex.push_back({i, PathORAM::fromText(to_string(i), ORAM_BLOCK_SIZE)});
			treeIndex.push_back({salaryToNumber(to_string(i)), BPlusTree::bytesFromNumber(i)});
		}

		for (number i = 0; i < COUNT / 10; i++)
		{
			queries.push_back({salaryToNumber(to_string(8 * i + 3)), salaryToNumber(to_string(8 * i + 8))});
		}
	}

	return {oramIndex, treeIndex, queries};
}

tuple<vector<ORAMSet>, shared_ptr<BPlusTree::AbsStorageAdapter>, shared_ptr<BPlusTree::Tree>>
constructIndices(vector<pair<number, bytes>> oramIndex, vector<pair<number, bytes>> treeIndex, bool generate)
{
	LOG(INFO,
		generate ?
			"Storing data in ORAM and generating B+ tree indices..." :
			"Reading ORAM and B+ tree data from files...");

	vector<vector<pair<number, bytes>>> oramIndexBrokenUp;
	oramIndexBrokenUp.resize(ORAMS_NUMBER);
	for (auto record : oramIndex)
	{
		oramIndexBrokenUp[record.first % ORAMS_NUMBER].push_back({record.first / ORAMS_NUMBER, record.second});
	}
	vector<ORAMSet> oramSets;
	for (auto i = 0; i < ORAMS_NUMBER; i++)
	{
		bytes oramKey;
		if (generate)
		{
			oramKey = PathORAM::getRandomBlock(KEYSIZE);
			PathORAM::storeKey(oramKey, filename(KEY_FILE, i));
		}
		else
		{
			oramKey = PathORAM::loadKey(filename(KEY_FILE, i));
		}

		shared_ptr<PathORAM::AbsStorageAdapter> oramStorage;
		switch (ORAM_STORAGE)
		{
			case InMemory:
				oramStorage = make_shared<PathORAM::InMemoryStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey);
				break;
			case FileSystem:
				oramStorage = make_shared<PathORAM::FileSystemStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, filename(ORAM_STORAGE_FILE, i), generate);
				break;
			case Redis:
				oramStorage = make_shared<PathORAM::RedisStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, redishost(ORAM_REDIS_HOST, i), generate);
				break;
			case Aerospike:
				oramStorage = make_shared<PathORAM::AerospikeStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, ORAM_AEROSPIKE_HOST, generate, to_string(i));
				break;
		}

		auto oramPositionMap = make_shared<PathORAM::InMemoryPositionMapAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z);
		if (!generate)
		{
			oramPositionMap->loadFromFile(filename(ORAM_MAP_FILE, i));
		}
		auto oramStash = make_shared<PathORAM::InMemoryStashAdapter>(3 * ORAM_LOG_CAPACITY * ORAM_Z);
		if (!generate)
		{
			oramStash->loadFromFile(filename(ORAM_STASH_FILE, i), ORAM_BLOCK_SIZE);
		}
		auto oram = make_shared<PathORAM::ORAM>(
			ORAM_LOG_CAPACITY,
			ORAM_BLOCK_SIZE,
			ORAM_Z,
			oramStorage,
			oramPositionMap,
			oramStash,
			generate);

		if (generate)
		{
			oram->load(oramIndexBrokenUp[i]);
			oramPositionMap->storeToFile(filename(ORAM_MAP_FILE, i));
			oramStash->storeToFile(filename(ORAM_STASH_FILE, i));
		}

		oramSets.push_back({oramStorage, oramPositionMap, oramStash, oram});
	}

	auto treeStorage = make_shared<BPlusTree::FileSystemStorageAdapter>(TREE_BLOCK_SIZE, filename(TREE_FILE, -1), generate);
	auto tree		 = generate ? make_shared<BPlusTree::Tree>(treeStorage, treeIndex) : make_shared<BPlusTree::Tree>(treeStorage);

	return {oramSets, treeStorage, tree};
}

void writeJson(vector<pair<number, number>> measurements)
{
	pt::ptree root;
	pt::ptree overheads;

	for (auto measurement : measurements)
	{
		pt::ptree overhead;
		overhead.put("overhead", measurement.first);
		overhead.put("queries", measurement.second);
		overheads.push_back({"", overhead});
	}
	root.put("COUNT", COUNT);
	root.put("ORAM_BLOCK_SIZE", ORAM_BLOCK_SIZE);
	root.put("ORAM_LOG_CAPACITY", ORAM_LOG_CAPACITY);
	root.put("ORAMS_NUMBER", ORAMS_NUMBER);
	root.put("PARALLEL", PARALLEL);
	root.put("ORAM_Z", ORAM_Z);
	root.put("TREE_BLOCK_SIZE", TREE_BLOCK_SIZE);
	root.put("ORAM_BACKEND", oramBackendStrings[ORAM_STORAGE]);

	auto timestamp = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count();
	root.put("TIMESTAMP", timestamp);
	root.add_child("queries", overheads);

	auto filename = boost::str(boost::format("./results/%1%.json") % timestamp);

	pt::write_json(filename, root);

	LOG(INFO, boost::format("Log written to %1%") % filename);
}

number salaryToNumber(string salary)
{
	auto salaryDouble = stod(salary) * 100;
	auto salaryNumber = (long long)salaryDouble + (LLONG_MAX / 4);
	return (number)salaryNumber;
}

double numberToSalary(number salary)
{
	return ((long long)salary - (LLONG_MAX / 4)) * 0.01;
}

string filename(string filename, int i)
{
	return filename + (i > -1 ? ("-" + to_string(i)) : "") + ".bin";
}

string redishost(string host, int i)
{
	return host + (i > -1 ? ("/" + to_string(i)) : "");
}

void LOG(LOG_LEVEL level, boost::format message)
{
	LOG(level, boost::str(message));
}

void LOG(LOG_LEVEL level, string message)
{
	if (level >= __logLevel)
	{
		auto t = time(nullptr);
		cout << "[" << put_time(localtime(&t), "%d/%m/%Y %H:%M:%S") << "] " << setw(8) << logLevelColors[level] << logLevelStrings[level] << ": " << message << RESET << endl;
	}
}
