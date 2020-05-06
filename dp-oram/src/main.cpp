#include "b-plus-tree/tree.hpp"
#include "b-plus-tree/utility.hpp"
#include "definitions.h"
#include "path-oram/oram.hpp"
#include "path-oram/utility.hpp"
#include "utility.hpp"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;
namespace pt = boost::property_tree;

wstring timeToString(long long time);
number salaryToNumber(string salary);
double numberToSalary(number salary);
string filename(string filename, int i);
string redishost(string host, int i);
template <class INPUT, class OUTPUT>
vector<OUTPUT> transform(vector<INPUT> input, function<OUTPUT(const INPUT&)> application);
wstring toWString(string input);

void LOG(LOG_LEVEL level, wstring message);
void LOG(LOG_LEVEL level, boost::wformat message);

#pragma region GLOBALS

auto COUNT					 = 1000uLL;
auto ORAM_BLOCK_SIZE		 = 256uLL;
auto ORAM_LOG_CAPACITY		 = 10uLL;
auto ORAMS_NUMBER			 = 1;
auto PARALLEL				 = true;
const auto ORAM_Z			 = 3uLL;
const auto TREE_BLOCK_SIZE	 = 64uLL;
auto ORAM_STORAGE			 = FileSystem;
auto USE_ORAMS				 = true;
const auto BATCH_SIZE		 = 1000;
const auto SYNTHETIC_QUERIES = 100;

const auto DP_K = 16;
auto DP_BETA	= 10uLL;
auto DP_EPSILON = 10;
auto DP_BUCKETS = 0uLL;

auto SEED = 1305;

number MIN_VALUE = ULONG_MAX;
number MAX_VALUE = 0;

const auto FILES_DIR		 = "./storage-files";
const auto KEY_FILE			 = "key";
const auto TREE_FILE		 = "tree";
const auto ORAM_STORAGE_FILE = "oram-storage";
const auto ORAM_MAP_FILE	 = "oram-map";
const auto ORAM_STASH_FILE	 = "oram-stash";

string REDIS_HOST	  = "tcp://127.0.0.1:6379";
string AEROSPIKE_HOST = "127.0.0.1";

const auto DATA_FILE  = "../../experiments-scripts/scripts/data.csv";
const auto QUERY_FILE = "../../experiments-scripts/scripts/query.csv";

#define LOG_PARAMETER(parameter) LOG(INFO, boost::wformat(L"%1% = %2%") % #parameter % parameter)
#define PUT_PARAMETER(parameter) root.put(#parameter, parameter);

#pragma endregion

int main(int argc, char* argv[])
{
	// to use wcout properly
	try
	{
		setlocale(LC_ALL, "en_US.utf8");
		locale loc("en_US.UTF-8");
		wcout.imbue(loc);
	}
	catch (...)
	{
		LOG(WARNING, L"Could not set locale: en_US.UTF-8");
	}

#pragma region COMMAND_LINE_ARGUMENTS

	auto oramsNumberCheck	= [](int v) { if (v < 1 || v > 96) { throw Exception("malformed --oramsNumber"); } };
	auto epsilonCheck		= [](int v) { if (v < 1 ) { throw Exception("malformed --epsilon, mist be >= 1"); } };
	auto betaCheck			= [](number v) { if (v < 1) { throw Exception("malformed --beta, must be >= 1"); } };
	auto bucketsNumberCheck = [](int v) {
		auto logV = log(v) / log(DP_K);
		if (ceil(logV) != floor(logV))
		{
			throw Exception(boost::format("malformed --bucketsNumber, must be a power of %1%") % DP_K);
		}
	};

	po::options_description desc("range query processor");
	desc.add_options()("help,h", "produce help message");
	desc.add_options()("generateIndices,g", po::value<bool>()->default_value(true), "if set, will generate ORAM and tree indices, otherwise will read files");
	desc.add_options()("readInputs,r", po::value<bool>()->default_value(true), "if set, will read inputs from files");
	desc.add_options()("parallel,p", po::value<bool>(&PARALLEL)->default_value(PARALLEL), "if set, will query orams in parallel");
	desc.add_options()("oramStorage,s", po::value<ORAM_BACKEND>(&ORAM_STORAGE)->default_value(ORAM_STORAGE), "the ORAM backend to use");
	desc.add_options()("oramsNumber,n", po::value<int>(&ORAMS_NUMBER)->notifier(oramsNumberCheck)->default_value(ORAMS_NUMBER), "the number of parallel ORAMs to use");
	desc.add_options()("bucketsNumber,b", po::value<number>(&DP_BUCKETS)->notifier(bucketsNumberCheck)->default_value(DP_BUCKETS), "the number of buckets for DP (if 0, will choose max buckets such that less than the domain size)");
	desc.add_options()("useOrams,u", po::value<bool>(&USE_ORAMS)->default_value(USE_ORAMS), "if set will use ORAMs, otherwise each query will download everythin every query");
	desc.add_options()("beta", po::value<number>(&DP_BETA)->notifier(betaCheck)->default_value(DP_BETA), "beta parameter for DP; x such that beta = 2^{-x}");
	desc.add_options()("epsilon", po::value<int>(&DP_EPSILON)->notifier(epsilonCheck)->default_value(DP_EPSILON), "epsilon parameter for DP");
	desc.add_options()("count", po::value<number>(&COUNT)->default_value(COUNT), "number of synthetic records to generate");
	desc.add_options()("verbosity,v", po::value<LOG_LEVEL>(&__logLevel)->default_value(INFO), "verbosity level to output");
	desc.add_options()("redis", po::value<string>(&REDIS_HOST)->default_value(REDIS_HOST), "Redis host to use");
	desc.add_options()("seed", po::value<int>(&SEED)->default_value(SEED), "To use if in DEBUG mode (otherwise OpenSSL will sample fresh randmoness)");
	desc.add_options()("aerospike", po::value<string>(&AEROSPIKE_HOST)->default_value(AEROSPIKE_HOST), "Aerospike host to use");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		cout << desc << "\n";
		exit(1);
	}

	if (
		ORAM_STORAGE == FileSystem && !USE_ORAMS && PARALLEL)
	{
		LOG(WARNING, L"Can't use FS strawman storage in parallel. PARALLEL will be set to false.");
		PARALLEL = false;
	}

	srand(SEED);

#pragma endregion

#pragma region GENERATE_DATA

	LOG(INFO, L"Constructing data set...");

	vector<pair<number, bytes>> oramIndex;
	vector<pair<number, bytes>> treeIndex;
	vector<pair<number, number>> queries;
	if (vm["readInputs"].as<bool>())
	{
		ifstream dataFile(DATA_FILE);

		string line = "";
		auto i		= 0;
		while (getline(dataFile, line))
		{
			vector<string> record;
			boost::algorithm::split(record, line, boost::is_any_of(","));
			auto salary = salaryToNumber(record[7]);
			MAX_VALUE	= max(salary, MAX_VALUE);
			MIN_VALUE	= min(salary, MIN_VALUE);

			LOG(ALL, boost::wformat(L"Salary: %9.2f, data length: %3i") % numberToSalary(salary) % line.size());

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

			LOG(ALL, boost::wformat(L"Query: {%9.2f, %9.2f}") % numberToSalary(left) % numberToSalary(right));

			queries.push_back({left, right});
		}
		queryFile.close();
	}
	else
	{
		for (number i = 0; i < COUNT; i++)
		{
			ostringstream text;
			for (auto j = 0; j < 10; j++)
			{
				text << to_string(i) + (j < 9 ? "," : "");
			}
			auto salary = salaryToNumber(to_string(i));
			oramIndex.push_back({i, PathORAM::fromText(text.str(), ORAM_BLOCK_SIZE)});
			treeIndex.push_back({salary, BPlusTree::bytesFromNumber(i)});

			MAX_VALUE = max(salary, MAX_VALUE);
			MIN_VALUE = min(salary, MIN_VALUE);
		}

		for (number i = 0; i < SYNTHETIC_QUERIES; i++)
		{
			queries.push_back({salaryToNumber(to_string(8 * i + 3)), salaryToNumber(to_string(8 * i + 8))});
		}
	}

	COUNT = oramIndex.size();

	auto sizes		= transform<pair<number, bytes>, int>(oramIndex, [](pair<number, bytes> val) { return val.second.size(); });
	ORAM_BLOCK_SIZE = *max_element(sizes.begin(), sizes.end());

	ORAM_LOG_CAPACITY = ceil(log2(COUNT / ORAMS_NUMBER)) + 1;

	LOG_PARAMETER(COUNT);
	LOG_PARAMETER(ORAM_BLOCK_SIZE);
	LOG_PARAMETER(ORAM_LOG_CAPACITY);
	LOG_PARAMETER(ORAMS_NUMBER);
	LOG_PARAMETER(PARALLEL);
	LOG_PARAMETER(ORAM_Z);
	LOG_PARAMETER(TREE_BLOCK_SIZE);
	LOG_PARAMETER(USE_ORAMS);
	LOG_PARAMETER(BATCH_SIZE);
	LOG_PARAMETER(SEED);
	LOG_PARAMETER(DP_K);
	LOG_PARAMETER(DP_BETA);
	LOG_PARAMETER(DP_EPSILON);

	LOG(INFO, boost::wformat(L"ORAM_BACKEND = %1%") % oramBackendStrings[ORAM_STORAGE]);
	LOG(INFO, boost::wformat(L"REDIS_HOST = %1%") % toWString(REDIS_HOST));
	LOG(INFO, boost::wformat(L"AEROSPIKE_HOST = %1%") % toWString(AEROSPIKE_HOST));

#pragma endregion

#pragma region CONSTRUCT_INDICES

	LOG(INFO,
		vm["generateIndices"].as<bool>() ?
			L"Generating indices..." :
			L"Reading from files...");

	if (vm["generateIndices"].as<bool>())
	{
		boost::filesystem::remove_all(FILES_DIR);
		boost::filesystem::create_directories(FILES_DIR);
	}

	vector<tuple<number, number, number>> measurements;

	if (USE_ORAMS)
	{
		vector<vector<pair<number, bytes>>> oramIndexBrokenUp;
		oramIndexBrokenUp.resize(ORAMS_NUMBER);
		for (auto record : oramIndex)
		{
			oramIndexBrokenUp[record.first % ORAMS_NUMBER].push_back({record.first / ORAMS_NUMBER, record.second});
		}

		auto loadOram = [](int i, vector<pair<number, bytes>> indices, bool generate, string redisHost, string aerospikeHost, promise<ORAMSet>* promise) -> void {
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
					oramStorage = make_shared<PathORAM::InMemoryStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, ORAM_Z);
					break;
				case FileSystem:
					oramStorage = make_shared<PathORAM::FileSystemStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, filename(ORAM_STORAGE_FILE, i), generate, ORAM_Z);
					break;
				case Redis:
					oramStorage = make_shared<PathORAM::RedisStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, redishost(redisHost, i), generate, ORAM_Z);
					break;
				case Aerospike:
					oramStorage = make_shared<PathORAM::AerospikeStorageAdapter>(((1 << ORAM_LOG_CAPACITY) * ORAM_Z) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, aerospikeHost, generate, ORAM_Z, to_string(i));
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
				generate,
				ULONG_MAX);

			if (generate)
			{
				oram->load(indices);
			}

			promise->set_value({oramStorage, oramPositionMap, oramStash, oram});
		};

		vector<ORAMSet> oramSets;
		thread threads[ORAMS_NUMBER];
		promise<ORAMSet> promises[ORAMS_NUMBER];
		future<ORAMSet> futures[ORAMS_NUMBER];

		for (auto i = 0; i < ORAMS_NUMBER; i++)
		{
			futures[i] = promises[i].get_future();
			threads[i] = thread(
				loadOram,
				i,
				oramIndexBrokenUp[i],
				vm["generateIndices"].as<bool>(),
				REDIS_HOST,
				AEROSPIKE_HOST,
				&promises[i]);
		}

		for (auto i = 0; i < ORAMS_NUMBER; i++)
		{
			auto result = futures[i].get();
			threads[i].join();
			oramSets.push_back(result);
		}

		auto treeStorage = make_shared<BPlusTree::FileSystemStorageAdapter>(TREE_BLOCK_SIZE, filename(TREE_FILE, -1), vm["generateIndices"].as<bool>());
		auto tree		 = vm["generateIndices"].as<bool>() ? make_shared<BPlusTree::Tree>(treeStorage, treeIndex) : make_shared<BPlusTree::Tree>(treeStorage);

		auto orams = transform<ORAMSet, shared_ptr<PathORAM::ORAM>>(oramSets, [](ORAMSet val) { return get<3>(val); });

#pragma endregion

#pragma region DP

		// IMPORTANT: salaries are in cents, but we still compute domain in dollars
		auto DP_DOMAIN = (MAX_VALUE - MIN_VALUE) / 100;
		if (DP_BUCKETS == 0)
		{
			DP_BUCKETS = 1;
			while (DP_BUCKETS * DP_K <= DP_DOMAIN)
			{
				DP_BUCKETS *= DP_K;
			}
		}

		auto DP_LEVELS = (int)(log(DP_BUCKETS) / log(DP_K));
		auto DP_MU	   = optimalMu(1.0 / (1 << DP_BETA), DP_K, DP_BUCKETS, DP_EPSILON);

		LOG_PARAMETER(DP_DOMAIN);
		LOG_PARAMETER(MIN_VALUE);
		LOG_PARAMETER(MAX_VALUE);
		LOG_PARAMETER(DP_BUCKETS);
		LOG_PARAMETER(DP_LEVELS);
		LOG_PARAMETER(DP_MU);

		vector<map<pair<number, number>, number>> noises;
		noises.resize(ORAMS_NUMBER);

		for (auto i = 0; i < ORAMS_NUMBER; i++)
		{
			auto buckets = DP_BUCKETS;
			for (auto l = 0; l < DP_LEVELS; l++)
			{
				for (auto j = 0uLL; j < buckets; j++)
				{
					noises[i][{l, j}] = (int)sampleLaplace(DP_MU, (double)DP_LEVELS / DP_EPSILON);
				}
				buckets /= DP_K;
			}
			noises[i][{DP_LEVELS, 0}] = 0.0; // root noise is zero, because downloading all data hides everything by definition
		}

#pragma endregion

#pragma region QUERY

		LOG(INFO, boost::wformat(L"Running %1% queries...") % queries.size());

		auto queryOram = [](vector<number> ids, shared_ptr<PathORAM::ORAM> oram, promise<vector<bytes>>* promise) -> vector<bytes> {
			vector<bytes> answer;
			if (ids.size() > 0)
			{
				auto requests = transform<number, pair<number, bytes>>(ids, [](number id) { return make_pair(id, bytes()); });
				answer		  = oram->multiple(requests);
			}

			if (promise != NULL)
			{
				promise->set_value(answer);
			}

			return answer;
		};

		auto queryIndex = 1;
		for (auto query : queries)
		{
			auto start = chrono::steady_clock::now();

			// DP padding
			auto [fromBucket, toBucket, from, to] = padToBuckets(query, MIN_VALUE, MAX_VALUE, DP_BUCKETS);

			auto oramIds = tree->search(from, to);

			// DP add noise
			auto noiseNodes = BRC(DP_K, fromBucket, toBucket);

			// add real block IDs
			vector<vector<number>> blockIds;
			blockIds.resize(ORAMS_NUMBER);
			for (auto oramId : oramIds)
			{
				auto blockId = BPlusTree::numberFromBytes(oramId);
				blockIds[blockId % ORAMS_NUMBER].push_back(blockId / ORAMS_NUMBER);
			}

			// add noisy fake block IDs
			auto totalNoise = 0uLL;
			for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
			{
				for (auto node : noiseNodes)
				{
					for (auto j = 0uLL; j < noises[i][node]; j++)
					{
						blockIds[i].push_back(PathORAM::getRandomUInt(COUNT / ORAMS_NUMBER));
					}
					totalNoise += noises[i][node];
				}
			}

			LOG(TRACE, boost::wformat(L"Query {%9.2f, %9.2f} was transformed to {%9.2f, %9.2f}, buckets [%4i, %4i], added total of %4i noisy records") % numberToSalary(query.first) % numberToSalary(query.second) % numberToSalary(from) % numberToSalary(to) % fromBucket % toBucket % totalNoise);

			vector<bytes> returned;
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
					returned.insert(returned.end(), result.begin(), result.end());
				}
			}
			else
			{
				for (auto i = 0; i < ORAMS_NUMBER; i++)
				{
					auto result = queryOram(blockIds[i], orams[i], NULL);
					returned.insert(returned.end(), result.begin(), result.end());
				}
			}
			auto count = 0;
			for (auto record : returned)
			{
				auto text = PathORAM::toText(record, ORAM_BLOCK_SIZE);

				vector<string> broken;
				boost::algorithm::split(broken, text, boost::is_any_of(","));
				auto salary = salaryToNumber(broken[7]);

				if (salary >= query.first && salary <= query.second)
				{
					count++;
				}
			}

			auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();
			measurements.push_back({elapsed, count, returned.size()});

			LOG(DEBUG, boost::wformat(L"Query %3i / %3i : {%9.2f, %9.2f} the result size is %6i (%6i with noise) (completed in %7s, or %7s μs per record)") % queryIndex % queries.size() % numberToSalary(query.first) % numberToSalary(query.second) % count % returned.size() % timeToString(elapsed) % (count > 0 ? timeToString(elapsed / count) : L"0 ns"));
			queryIndex++;
		}

		for (auto i = 0uLL; i < oramSets.size(); i++)
		{
			static_pointer_cast<PathORAM::InMemoryPositionMapAdapter>(get<1>(oramSets[i]))->storeToFile(filename(ORAM_MAP_FILE, i));
			static_pointer_cast<PathORAM::InMemoryStashAdapter>(get<2>(oramSets[i]))->storeToFile(filename(ORAM_STASH_FILE, i));
		}

#pragma endregion
	}
	else
	{
#pragma region STRAWMAN
		bytes storageKey;
		if (vm["generateIndices"].as<bool>())
		{
			storageKey = PathORAM::getRandomBlock(KEYSIZE);
			PathORAM::storeKey(storageKey, filename(KEY_FILE, -1));
		}
		else
		{
			storageKey = PathORAM::loadKey(filename(KEY_FILE, -1));
		}

		shared_ptr<PathORAM::AbsStorageAdapter> storage;
		switch (ORAM_STORAGE)
		{
			case InMemory:
				storage = make_shared<PathORAM::InMemoryStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, 1);
				break;
			case FileSystem:
				storage = make_shared<PathORAM::FileSystemStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, filename(ORAM_STORAGE_FILE, -1), vm["generateIndices"].as<bool>(), 1);
				break;
			case Redis:
				storage = make_shared<PathORAM::RedisStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, redishost(REDIS_HOST, -1), vm["generateIndices"].as<bool>(), 1);
				break;
			case Aerospike:
				storage = make_shared<PathORAM::AerospikeStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, AEROSPIKE_HOST, vm["generateIndices"].as<bool>(), 1);
				break;
		}

		vector<pair<number, vector<pair<number, bytes>>>> batch;
		for (number i = 0; i < oramIndex.size(); i++)
		{
			batch.push_back({i, {oramIndex[i]}});
			if (i % BATCH_SIZE == 0 || i == oramIndex.size() - 1)
			{
				if (batch.size() > 0)
				{
					storage->set(batch);
				}
				batch.clear();
			}
		}

		auto storageQuery = [storage](number indexFrom, number indexTo, number queryFrom, number queryTo, promise<vector<string>>* promise) -> vector<string> {
			vector<string> answer;

			vector<number> batch;
			for (auto i = indexFrom; i < indexTo; i++)
			{
				batch.push_back(i);
				if (i % BATCH_SIZE == 0 || i == indexTo - 1)
				{
					if (batch.size() > 0)
					{
						auto returned = storage->get(batch);
						for (auto record : returned)
						{
							auto text = PathORAM::toText(record.second, ORAM_BLOCK_SIZE);

							vector<string> broken;
							boost::algorithm::split(broken, text, boost::is_any_of(","));
							auto salary = salaryToNumber(broken[7]);

							if (salary >= queryFrom && salary <= queryTo)
							{
								answer.push_back(text);
							}
						}
						batch.clear();
					}
				}
			}

			if (promise != NULL)
			{
				promise->set_value(answer);
			}

			return answer;
		};

		for (auto query : queries)
		{
			auto start = chrono::steady_clock::now();

			auto count = 0;
			if (PARALLEL)
			{
				thread threads[ORAMS_NUMBER];
				promise<vector<string>> promises[ORAMS_NUMBER];
				future<vector<string>> futures[ORAMS_NUMBER];

				for (auto i = 0; i < ORAMS_NUMBER; i++)
				{
					futures[i] = promises[i].get_future();
					threads[i] = thread(storageQuery, i * COUNT / ORAMS_NUMBER, (i + 1) * COUNT / ORAMS_NUMBER, query.first, query.second, &promises[i]);
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
					auto result = storageQuery(i * COUNT / ORAMS_NUMBER, (i + 1) * COUNT / ORAMS_NUMBER, query.first, query.second, NULL);
					count += result.size();
				}
			}

			auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();
			measurements.push_back({elapsed, count, COUNT});

			LOG(DEBUG, boost::wformat(L"For query {%9.2f, %9.2f} the result size is %3i (completed in %7s, or %7s per record)") % numberToSalary(query.first) % numberToSalary(query.second) % count % timeToString(elapsed) % (count > 0 ? timeToString(elapsed / count) : L"0 ns"));
		}
#pragma endregion
	}

	LOG(INFO, L"Complete!");

	auto overheads = transform<tuple<number, number, number>, number>(measurements, [](tuple<number, number, number> val) { return get<0>(val); });
	auto counts	   = transform<tuple<number, number, number>, number>(measurements, [](tuple<number, number, number> val) { return get<1>(val); });

	auto sum		   = accumulate(overheads.begin(), overheads.end(), 0LL);
	auto average	   = sum / overheads.size();
	auto perResultItem = sum / accumulate(counts.begin(), counts.end(), 0LL);

#pragma region WRITE_JSON

	LOG(INFO, boost::wformat(L"Total: %1%, average: %2% per query, %3% per result item") % timeToString(sum) % timeToString(average) % timeToString(perResultItem));

	wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

	pt::ptree root;
	pt::ptree overheadsNode;

	for (auto measurement : measurements)
	{
		pt::ptree overhead;
		overhead.put("overhead", get<0>(measurement));
		overhead.put("realCount", get<1>(measurement));
		overhead.put("noisyCount", get<2>(measurement));
		overheadsNode.push_back({"", overhead});
	}

	PUT_PARAMETER(COUNT);
	PUT_PARAMETER(ORAM_BLOCK_SIZE);
	PUT_PARAMETER(ORAM_LOG_CAPACITY);
	PUT_PARAMETER(ORAMS_NUMBER);
	PUT_PARAMETER(PARALLEL);
	PUT_PARAMETER(ORAM_Z);
	PUT_PARAMETER(TREE_BLOCK_SIZE);
	PUT_PARAMETER(USE_ORAMS);
	PUT_PARAMETER(BATCH_SIZE);
	PUT_PARAMETER(SEED);
	PUT_PARAMETER(DP_BUCKETS);
	PUT_PARAMETER(DP_K);
	PUT_PARAMETER(DP_BETA);
	PUT_PARAMETER(DP_EPSILON);

	root.put("ORAM_BACKEND", converter.to_bytes(oramBackendStrings[ORAM_STORAGE]));
	root.put("REDIS", REDIS_HOST);
	root.put("AEROSPIKE", AEROSPIKE_HOST);

	auto timestamp = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	root.put("TIMESTAMP", timestamp);

	pt::ptree aggregates;
	aggregates.put("totalElapsed", sum);
	aggregates.put("perQuery", average);
	aggregates.put("perResultItem", perResultItem);
	root.add_child("aggregates", aggregates);

	root.add_child("queries", overheadsNode);

	auto rawtime = time(nullptr);
	stringstream timestream;
	timestream << put_time(localtime(&rawtime), "%Y-%m-%d-%H-%M-%S");

	auto filename = boost::str(boost::format("./results/%1%-%2%.json") % timestream.str() % timestamp);

	pt::write_json(filename, root);

	LOG(INFO, boost::wformat(L"Log written to %1%") % converter.from_bytes(filename));

#pragma endregion

	return 0;
}

#pragma region HELPERS

template <class INPUT, class OUTPUT>
vector<OUTPUT> transform(vector<INPUT> input, function<OUTPUT(const INPUT&)> application)
{
	vector<OUTPUT> output;
	output.resize(input.size());
	transform(input.begin(), input.end(), output.begin(), application);

	return output;
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
	return string(FILES_DIR) + "/" + filename + (i > -1 ? ("-" + to_string(i)) : "") + ".bin";
}

string redishost(string host, int i)
{
	return host + (i > -1 ? ("/" + to_string(i)) : "");
}

wstring toWString(string input)
{
	wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.from_bytes(input);
}

void LOG(LOG_LEVEL level, boost::wformat message)
{
	LOG(level, boost::str(message));
}

void LOG(LOG_LEVEL level, wstring message)
{
	if (level >= __logLevel)
	{
		auto t = time(nullptr);
		wcout << L"[" << put_time(localtime(&t), L"%d/%m/%Y %H:%M:%S") << "] " << setw(8) << logLevelColors[level] << logLevelStrings[level] << L": " << message << RESET << endl;
	}
}

#pragma endregion
