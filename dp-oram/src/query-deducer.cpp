#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "utility.hpp"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <iomanip>
#include <iostream>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;

const auto INPUT_FILES_DIR = string("../../experiments-scripts/output/");
const auto FILES_DIR	   = string("./storage-files");

auto DATASET_TAG  = string("270K-1.7M-uniform");
auto QUERYSET_TAG = string("270K-1.7M-uniform-500");

auto GENERATE_INDICES = true;
auto VERBOSE		  = false;

const auto TREE_BLOCK_SIZE = 64uLL;
const auto TREE_FILE	   = string("tree");
number DATA_SIZE;

int main(int argc, char* argv[])
{
	po::options_description desc("Query deducer", 120);
	desc.add_options()("help,h", "produce help message");
	desc.add_options()("generate,g", po::value<bool>(&GENERATE_INDICES)->default_value(GENERATE_INDICES), "if set, will (re)generate tree indices, otherwise will read files");
	desc.add_options()("verbose,v", po::value<bool>(&VERBOSE)->default_value(VERBOSE), "if set, will print query results sizes");
	desc.add_options()("size", po::value<number>(&DATA_SIZE)->required(), "the number of datapoints to correctly compute selectivity");
	desc.add_options()("dataset", po::value<string>(&DATASET_TAG)->default_value(DATASET_TAG), "the dataset tag to use when reading dataset file");
	desc.add_options()("queryset", po::value<string>(&QUERYSET_TAG)->default_value(QUERYSET_TAG), "the queryset tag to use when reading queryset file");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << "\n";
		exit(1);
	}

	auto treeStorage = make_shared<BPlusTree::FileSystemStorageAdapter>(TREE_BLOCK_SIZE, (boost::filesystem::path(FILES_DIR) / (TREE_FILE + ".bin")).string(), GENERATE_INDICES);
	shared_ptr<BPlusTree::Tree> tree;

	if (GENERATE_INDICES)
	{
		cout << "Generating tree" << endl;

		vector<pair<number, bytes>> treeIndex;

		auto dataFilePath = (boost::filesystem::path(INPUT_FILES_DIR) / (DATASET_TAG + ".csv")).string();
		ifstream dataFile(dataFilePath);
		if (!dataFile.is_open())
		{
			cerr << "File cannot be opened: " << dataFilePath << endl;
			exit(1);
		}

		string line = "";
		while (getline(dataFile, line))
		{
			treeIndex.push_back({salaryToNumber(line), bytes()});
		}
		dataFile.close();

		tree = make_shared<BPlusTree::Tree>(treeStorage, treeIndex);

		cout << "Size: " << treeIndex.size() << endl;
	}
	else
	{
		tree = make_shared<BPlusTree::Tree>(treeStorage);
	}

	cout << "Reading queries" << endl;

	auto queryFilePath = (boost::filesystem::path(INPUT_FILES_DIR) / (QUERYSET_TAG + ".csv")).string();
	ifstream queryFile(queryFilePath);
	if (!queryFile.is_open())
	{
		cerr << "File cannot be opened: " << queryFilePath << endl;
		exit(1);
	}

	vector<pair<number, number>> queries;
	string line = "";
	while (getline(queryFile, line))
	{
		vector<string> query;
		boost::algorithm::split(query, line, boost::is_any_of(","));
		auto left  = salaryToNumber(query[0]);
		auto right = salaryToNumber(query[1]);

		queries.push_back({left, right});
	}
	queryFile.close();

	vector<number> responseSizes;
	for (auto&& query : queries)
	{
		vector<bytes> response;
		tree->search(query.first, query.second, response);
		responseSizes.push_back(response.size());

		if (VERBOSE)
		{
			cout << response.size() << "\t";
		}
	}
	if (VERBOSE)
	{
		cout << endl;
	}

	double sum	= accumulate(responseSizes.begin(), responseSizes.end(), 0.0);
	double mean = sum / responseSizes.size();

	double sq_sum = inner_product(responseSizes.begin(), responseSizes.end(), responseSizes.begin(), 0.0);
	double stddev = sqrt(sq_sum / responseSizes.size() - mean * mean);

	cout << "For " << DATA_SIZE << " datapoints, average selectivity is " << (mean / DATA_SIZE) * 100 << "%, stddev: " << stddev << ", avg result size: " << mean << endl;

	return 0;
}
