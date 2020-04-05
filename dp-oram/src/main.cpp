#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"

#include <boost/program_options.hpp>
#include <iostream>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;

void LOG(LOG_LEVEL level, string message);
void LOG(LOG_LEVEL level, boost::format message);

int main(int argc, char* argv[])
{
	po::options_description desc("range query processor");
	desc.add_options()("help", "produce help message");
	desc.add_options()("generateIndices", po::value<bool>()->default_value(true), "if set, will generate ORAM and tree indices, otherwise will read files");
	desc.add_options()("query", po::value<bool>()->default_value(true), "if set, will run queries");
	desc.add_options()("verbosity", po::value<LOG_LEVEL>(&__logLevel)->default_value(INFO), "verbosity level to output");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << "\n";
		return 1;
	}

	LOG(INFO, "Constructing data set...");

	const auto COUNT			 = 1000uLL;
	const auto ORAM_BLOCK_SIZE	 = 256uLL;
	const auto ORAM_LOG_CAPACITY = 10uLL;
	const auto ORAM_Z			 = 3uLL;
	const auto TREE_BLOCK_SIZE	 = 64uLL;

	const auto KEY_FILE			 = "key.bin";
	const auto TREE_FILE		 = "tree.bin";
	const auto ORAM_STORAGE_FILE = "oram-storage.bin";
	const auto ORAM_MAP_FILE	 = "oram-map.bin";
	const auto ORAM_STASH_FILE	 = "oram-stash.bin";

	vector<pair<number, bytes>> data;
	for (number i = 0; i < COUNT; i++)
	{
		data.push_back({i, PathORAM::fromText(to_string(i), ORAM_BLOCK_SIZE)});
	}

	LOG(INFO,
		vm["generateIndices"].as<bool>() ?
			"Storing data in ORAM and generating B+ tree indices..." :
			"Reading ORAM and B+ tree data from files...");

	bytes oramKey;
	if (vm["generateIndices"].as<bool>())
	{
		oramKey = PathORAM::getRandomBlock(KEYSIZE);
		PathORAM::storeKey(oramKey, KEY_FILE);
	}
	else
	{
		oramKey = PathORAM::loadKey(KEY_FILE);
	}

	auto oramStorage	 = new PathORAM::FileSystemStorageAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, ORAM_STORAGE_FILE, vm["generateIndices"].as<bool>());
	auto oramPositionMap = new PathORAM::InMemoryPositionMapAdapter(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z);
	if (!vm["generateIndices"].as<bool>())
	{
		oramPositionMap->loadFromFile(ORAM_MAP_FILE);
	}
	auto oramStash = new PathORAM::InMemoryStashAdapter(3 * ORAM_LOG_CAPACITY * ORAM_Z);
	if (!vm["generateIndices"].as<bool>())
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
		vm["generateIndices"].as<bool>());

	if (vm["generateIndices"].as<bool>())
	{
		oram->load(data);
		oramPositionMap->storeToFile(ORAM_MAP_FILE);
		oramStash->storeToFile(ORAM_STASH_FILE);
	}

	auto treeStorage = new BPlusTree::FileSystemStorageAdapter(TREE_BLOCK_SIZE, TREE_FILE, vm["generateIndices"].as<bool>());
	BPlusTree::Tree* tree;

	if (vm["generateIndices"].as<bool>())
	{
		vector<pair<number, bytes>> index;
		for (auto record : data)
		{
			index.push_back({record.first, BPlusTree::bytesFromNumber(record.first)});
		}

		tree = new BPlusTree::Tree(treeStorage, index);
	}
	else
	{
		tree = new BPlusTree::Tree(treeStorage);
	}

	// query
	if (vm["query"].as<bool>())
	{
		const auto QUERY = 5;
		auto oramId		 = BPlusTree::numberFromBytes(tree->search(QUERY)[0]);
		auto block		 = oram->get(oramId);
		auto result		 = PathORAM::toText(block, ORAM_BLOCK_SIZE);

		LOG(INFO, boost::format("For query %1% the result is %2%") % QUERY % result);
	}
	else
	{
		LOG(INFO, "Query stage skipped...");
	}

	delete oramStorage;
	delete oramPositionMap;
	delete oramStash;
	delete oram;

	delete treeStorage;
	delete tree;

	return 0;
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
