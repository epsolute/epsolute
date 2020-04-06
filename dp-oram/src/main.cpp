#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <boost/program_options.hpp>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;

po::variables_map setupArgs(int argc, char* argv[]);
void query(vector<PathORAM::ORAM*> orams, BPlusTree::Tree* tree, vector<pair<number, number>> queries);
tuple<vector<pair<number, bytes>>, vector<pair<number, bytes>>, vector<pair<number, number>>> generateData(bool read);
tuple<vector<ORAMSet>, BPlusTree::AbsStorageAdapter*, BPlusTree::Tree*> constructIndices(vector<pair<number, bytes>> oramIndex, vector<pair<number, bytes>> treeIndex, bool generate);

number salaryToNumber(string salary);
double numberToSalary(number salary);
string filename(string filename, int i);

void LOG(LOG_LEVEL level, string message);
void LOG(LOG_LEVEL level, boost::format message);

auto COUNT				   = 1000uLL;
auto ORAM_BLOCK_SIZE	   = 256uLL;
auto ORAM_LOG_CAPACITY	   = 10uLL;
auto ORAMS_NUMBER		   = 1;
auto PARALLEL			   = true;
const auto ORAM_Z		   = 3uLL;
const auto TREE_BLOCK_SIZE = 64uLL;

const auto KEY_FILE			 = "key";
const auto TREE_FILE		 = "tree";
const auto ORAM_STORAGE_FILE = "oram-storage";
const auto ORAM_MAP_FILE	 = "oram-map";
const auto ORAM_STASH_FILE	 = "oram-stash";

const auto DATA_FILE  = "../../experiments-scripts/scripts/data.csv";
const auto QUERY_FILE = "../../experiments-scripts/scripts/query.csv";

int main(int argc, char* argv[])
{
	auto vm = setupArgs(argc, argv);

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

	auto [oramSets, treeStorage, tree] = constructIndices(oramIndex, treeIndex, vm["generateIndices"].as<bool>());

	vector<PathORAM::ORAM*> orams;
	orams.resize(oramSets.size());
	transform(oramSets.begin(), oramSets.end(), orams.begin(), [](ORAMSet val) { return get<3>(val); });
	query(orams, tree, queries);

	LOG(INFO, "Complete!");

	for (auto set : oramSets)
	{
		apply([](auto&&... args) { ((delete args), ...); }, set);
	}

	delete treeStorage;
	delete tree;

	return 0;
}

po::variables_map setupArgs(int argc, char* argv[])
{
	po::options_description desc("range query processor");
	desc.add_options()("help", "produce help message");
	desc.add_options()("generateIndices", po::value<bool>()->default_value(true), "if set, will generate ORAM and tree indices, otherwise will read files");
	desc.add_options()("readInputs", po::value<bool>()->default_value(true), "if set, will read inputs from files");
	desc.add_options()("parallel", po::value<bool>(&PARALLEL)->default_value(true), "if set, will query orams in parallel");
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

void query(vector<PathORAM::ORAM*> orams, BPlusTree::Tree* tree, vector<pair<number, number>> queries)
{
	LOG(INFO, boost::format("Running %1% queries...") % queries.size());

	auto queryOram = [](vector<number> ids, PathORAM::ORAM* oram, promise<vector<bytes>>* promise) -> vector<bytes> {
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

		LOG(TRACE, boost::format("For query {%1%, %2%} the result size is %3%") % numberToSalary(query.first) % numberToSalary(query.second) % count);
	}
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

			LOG(TRACE, boost::format("Salary: %1%, data length: %2%") % numberToSalary(salary) % line.size());

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

			LOG(TRACE, boost::format("Query: {%1%, %2%}") % numberToSalary(left) % numberToSalary(right));

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

tuple<
	vector<ORAMSet>,
	BPlusTree::AbsStorageAdapter*,
	BPlusTree::Tree*>
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

		auto oramStorage	 = new PathORAM::FileSystemStorageAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, filename(ORAM_STORAGE_FILE, i), generate);
		auto oramPositionMap = new PathORAM::InMemoryPositionMapAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z);
		if (!generate)
		{
			oramPositionMap->loadFromFile(filename(ORAM_MAP_FILE, i));
		}
		auto oramStash = new PathORAM::InMemoryStashAdapter(3 * ORAM_LOG_CAPACITY * ORAM_Z);
		if (!generate)
		{
			oramStash->loadFromFile(filename(ORAM_STASH_FILE, i), ORAM_BLOCK_SIZE);
		}
		auto oram = new PathORAM::ORAM(
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

	auto treeStorage	  = new BPlusTree::FileSystemStorageAdapter(TREE_BLOCK_SIZE, filename(TREE_FILE, -1), generate);
	BPlusTree::Tree* tree = generate ? new BPlusTree::Tree(treeStorage, treeIndex) : new BPlusTree::Tree(treeStorage);

	return {oramSets, treeStorage, tree};
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

void LOG(LOG_LEVEL level, boost::format message)
{
	LOG(level, boost::str(message));
}

void LOG(LOG_LEVEL level, string message)
{
	if (level >= __logLevel)
	{
		auto t = time(nullptr);
		cout << "[" << put_time(localtime(&t), "%d/%m/%Y %H:%M:%S") << "] " << setw(8) << logLevelStrings[level] << ": " << message << endl;
	}
}
