#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <boost/program_options.hpp>
#include <ctime>
#include <iomanip>
#include <iostream>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;

po::variables_map setupArgs(int argc, char* argv[]);
void query(PathORAM::ORAM* oram, BPlusTree::Tree* tree, vector<pair<number, number>> queries);
tuple<vector<pair<number, bytes>>, vector<pair<number, bytes>>, vector<pair<number, number>>> generateData(bool read);
tuple<PathORAM::AbsStorageAdapter*, PathORAM::AbsPositionMapAdapter*, PathORAM::AbsStashAdapter*, PathORAM::ORAM*, BPlusTree::AbsStorageAdapter*, BPlusTree::Tree*> constructIndices(vector<pair<number, bytes>> oramIndex, vector<pair<number, bytes>> treeIndex, bool generate);

number salaryToNumber(string salary);
double numberToSalary(number salary);

void LOG(LOG_LEVEL level, string message);
void LOG(LOG_LEVEL level, boost::format message);

auto COUNT				   = 1000uLL;
auto ORAM_BLOCK_SIZE	   = 256uLL;
auto ORAM_LOG_CAPACITY	   = 10uLL;
const auto ORAM_Z		   = 3uLL;
const auto TREE_BLOCK_SIZE = 64uLL;

const auto KEY_FILE			 = "key.bin";
const auto TREE_FILE		 = "tree.bin";
const auto ORAM_STORAGE_FILE = "oram-storage.bin";
const auto ORAM_MAP_FILE	 = "oram-map.bin";
const auto ORAM_STASH_FILE	 = "oram-stash.bin";

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

	ORAM_LOG_CAPACITY = ceil(log2(COUNT)) + 1;

	LOG(INFO, boost::format("COUNT = %1%") % COUNT);
	LOG(INFO, boost::format("ORAM_BLOCK_SIZE = %1%") % ORAM_BLOCK_SIZE);
	LOG(INFO, boost::format("ORAM_LOG_CAPACITY = %1%") % ORAM_LOG_CAPACITY);
	LOG(INFO, boost::format("ORAM_Z = %1%") % ORAM_Z);
	LOG(INFO, boost::format("TREE_BLOCK_SIZE = %1%") % TREE_BLOCK_SIZE);

	auto [oramStorage, oramPositionMap, oramStash, oram, treeStorage, tree] = constructIndices(oramIndex, treeIndex, vm["generateIndices"].as<bool>());
	query(oram, tree, queries);

	LOG(INFO, "Complete!");

	delete oramStorage;
	delete oramPositionMap;
	delete oramStash;
	delete oram;

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

void query(PathORAM::ORAM* oram, BPlusTree::Tree* tree, vector<pair<number, number>> queries)
{
	LOG(INFO, boost::format("Running %1% queries...") % queries.size());

	for (auto query : queries)
	{
		auto oramIds = tree->search(query.first, query.second);
		auto count	 = 0;
		for (auto oramId : oramIds)
		{
			auto block	= oram->get(BPlusTree::numberFromBytes(oramId));
			auto result = PathORAM::toText(block, ORAM_BLOCK_SIZE);
			count++;
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
	PathORAM::AbsStorageAdapter*,
	PathORAM::AbsPositionMapAdapter*,
	PathORAM::AbsStashAdapter*,
	PathORAM::ORAM*,
	BPlusTree::AbsStorageAdapter*,
	BPlusTree::Tree*>
constructIndices(vector<pair<number, bytes>> oramIndex, vector<pair<number, bytes>> treeIndex, bool generate)
{
	LOG(INFO,
		generate ?
			"Storing data in ORAM and generating B+ tree indices..." :
			"Reading ORAM and B+ tree data from files...");

	bytes oramKey;
	if (generate)
	{
		oramKey = PathORAM::getRandomBlock(KEYSIZE);
		PathORAM::storeKey(oramKey, KEY_FILE);
	}
	else
	{
		oramKey = PathORAM::loadKey(KEY_FILE);
	}

	auto oramStorage	 = new PathORAM::FileSystemStorageAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, ORAM_STORAGE_FILE, generate);
	auto oramPositionMap = new PathORAM::InMemoryPositionMapAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z);
	if (!generate)
	{
		oramPositionMap->loadFromFile(ORAM_MAP_FILE);
	}
	auto oramStash = new PathORAM::InMemoryStashAdapter(3 * ORAM_LOG_CAPACITY * ORAM_Z);
	if (!generate)
	{
		oramStash->loadFromFile(ORAM_STASH_FILE, ORAM_BLOCK_SIZE);
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
		oram->load(oramIndex);
		oramPositionMap->storeToFile(ORAM_MAP_FILE);
		oramStash->storeToFile(ORAM_STASH_FILE);
	}

	auto treeStorage	  = new BPlusTree::FileSystemStorageAdapter(TREE_BLOCK_SIZE, TREE_FILE, generate);
	BPlusTree::Tree* tree = generate ? new BPlusTree::Tree(treeStorage, treeIndex) : new BPlusTree::Tree(treeStorage);

	return {oramStorage, oramPositionMap, oramStash, oram, treeStorage, tree};
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
