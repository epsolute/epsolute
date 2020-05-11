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
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <thread>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;
namespace pt = boost::property_tree;

// WARNING: this is supposed to be greater than the absolute value of the smallest element in the dataset
#define OFFSET 20000000

wstring timeToString(long long time);
number salaryToNumber(string salary);
double numberToSalary(number salary);
string filename(string filename, int i);
string redishost(string host, int i);
template <class INPUT, class OUTPUT>
vector<OUTPUT> transform(vector<INPUT>& input, function<OUTPUT(const INPUT&)> application);
wstring toWString(string input);

inline void storeInputs(vector<pair<number, number>>& queries, vector<number> oramBlockNumbers);
inline pair<vector<pair<number, number>>, vector<number>> loadInputs();

void LOG(LOG_LEVEL level, wstring message);
void LOG(LOG_LEVEL level, boost::wformat message);

#pragma region GLOBALS

auto COUNT					 = 1000uLL;
auto ORAM_BLOCK_SIZE		 = 256uLL;
auto ORAM_LOG_CAPACITY		 = 10uLL;
auto ORAMS_NUMBER			 = 1uLL;
auto PARALLEL				 = true;
const auto ORAM_Z			 = 3uLL;
const auto TREE_BLOCK_SIZE	 = 64uLL;
auto ORAM_STORAGE			 = FileSystem;
auto USE_ORAMS				 = true;
auto VIRTUAL_REQUESTS		 = false;
const auto BATCH_SIZE		 = 1000;
const auto SYNTHETIC_QUERIES = 20;

auto READ_INPUTS	  = true;
auto GENERATE_INDICES = true;

const auto DP_K	  = 16;
auto DP_BETA	  = 10uLL;
auto DP_EPSILON	  = 10;
auto DP_BUCKETS	  = 0uLL;
auto DP_USE_GAMMA = false;

auto SEED = 1305;

number MIN_VALUE = ULONG_MAX;
number MAX_VALUE = 0;

const auto FILES_DIR		 = "./storage-files";
const auto KEY_FILE			 = "key";
const auto TREE_FILE		 = "tree";
const auto ORAM_STORAGE_FILE = "oram-storage";
const auto ORAM_MAP_FILE	 = "oram-map";
const auto ORAM_STASH_FILE	 = "oram-stash";
const auto STATS_INPUT_FILE	 = "stats-input";
const auto QUERY_INPUT_FILE	 = "query-input";

string REDIS_HOST	  = "tcp://127.0.0.1:6379";
string AEROSPIKE_HOST = "127.0.0.1";

const auto DATA_FILE  = "../../experiments-scripts/scripts/data.csv";
const auto QUERY_FILE = "../../experiments-scripts/scripts/query.csv";

auto FILE_LOGGING = false;
string logName;
ofstream logFile;

auto SIGINT_RECEIVED = false;

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

	auto oramsNumberCheck	= [](number v) { if (v < 1) { throw Exception("malformed --oramsNumber"); } };
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
	desc.add_options()("generateIndices,g", po::value<bool>(&GENERATE_INDICES)->default_value(GENERATE_INDICES), "if set, will generate ORAM and tree indices, otherwise will read files");
	desc.add_options()("readInputs,r", po::value<bool>(&READ_INPUTS)->default_value(READ_INPUTS), "if set, will read inputs from files");
	desc.add_options()("parallel,p", po::value<bool>(&PARALLEL)->default_value(PARALLEL), "if set, will query orams in parallel");
	desc.add_options()("oramStorage,s", po::value<ORAM_BACKEND>(&ORAM_STORAGE)->default_value(ORAM_STORAGE), "the ORAM backend to use");
	desc.add_options()("oramsNumber,n", po::value<number>(&ORAMS_NUMBER)->notifier(oramsNumberCheck)->default_value(ORAMS_NUMBER), "the number of parallel ORAMs to use");
	desc.add_options()("bucketsNumber,b", po::value<number>(&DP_BUCKETS)->notifier(bucketsNumberCheck)->default_value(DP_BUCKETS), "the number of buckets for DP (if 0, will choose max buckets such that less than the domain size)");
	desc.add_options()("useOrams,u", po::value<bool>(&USE_ORAMS)->default_value(USE_ORAMS), "if set will use ORAMs, otherwise each query will download everythin every query");
	desc.add_options()("virtualRequests", po::value<bool>(&VIRTUAL_REQUESTS)->default_value(VIRTUAL_REQUESTS), "if set will only simulate ORAM queries, not actually make them");
	desc.add_options()("beta", po::value<number>(&DP_BETA)->notifier(betaCheck)->default_value(DP_BETA), "beta parameter for DP; x such that beta = 2^{-x}");
	desc.add_options()("epsilon", po::value<int>(&DP_EPSILON)->notifier(epsilonCheck)->default_value(DP_EPSILON), "epsilon parameter for DP");
	desc.add_options()("useGamma", po::value<bool>(&DP_USE_GAMMA)->default_value(DP_USE_GAMMA), "if set, will use Gamma method to add noise per ORAM");
	desc.add_options()("count", po::value<number>(&COUNT)->default_value(COUNT), "number of synthetic records to generate");
	desc.add_options()("verbosity,v", po::value<LOG_LEVEL>(&__logLevel)->default_value(INFO), "verbosity level to output");
	desc.add_options()("fileLogging", po::value<bool>(&FILE_LOGGING)->default_value(FILE_LOGGING), "if set, log stream will be duplicated to file (noticeably slows down simulation)");
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

	// open log file
	auto timestamp = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	auto rawtime   = time(nullptr);
	stringstream timestream;
	timestream << put_time(localtime(&rawtime), "%Y-%m-%d-%H-%M-%S");
	auto logName = boost::str(boost::format("%1%--%2%--%3%") % SEED % timestream.str() % timestamp);

	if (FILE_LOGGING)
	{
		logFile.open(boost::str(boost::format("./results/%1%.log") % logName), ios::out);
	}

	if (
		ORAM_STORAGE == FileSystem && !USE_ORAMS && PARALLEL)
	{
		LOG(WARNING, L"Can't use FS strawman storage in parallel. PARALLEL will be set to false.");
		PARALLEL = false;
	}

	struct stat buffer;
	// if stat file does not exist and GENERATE_INDICES == false
	if (stat(filename(STATS_INPUT_FILE, -1).c_str(), &buffer) != 0 && !GENERATE_INDICES)
	{
		LOG(WARNING, L"No stats file found and indices generation is disabled. Enabling it forcefully.");
		GENERATE_INDICES = true;
	}

	LOG(INFO, GENERATE_INDICES ? L"Generating indices..." : L"Reading from input files...");

	if (GENERATE_INDICES)
	{
		boost::filesystem::remove_all(FILES_DIR);
		boost::filesystem::create_directories(FILES_DIR);
	}

	srand(SEED);

#pragma endregion

#pragma region GENERATE_DATA

	LOG(INFO, L"Constructing data set...");

	vector<vector<pair<number, bytes>>> oramsIndex;
	vector<number> oramBlockNumbers;
	oramsIndex.resize(ORAMS_NUMBER);
	oramBlockNumbers.resize(ORAMS_NUMBER);

	// vector<pair<salary, bytes(ORAMid, blockId)>>
	vector<pair<number, bytes>> treeIndex;
	vector<pair<number, number>> queries;

	if (GENERATE_INDICES)
	{
		if (READ_INPUTS)
		{
			ifstream dataFile(DATA_FILE);

			string line = "";
			while (getline(dataFile, line))
			{
				vector<string> record;
				boost::algorithm::split(record, line, boost::is_any_of(","));
				auto salary = salaryToNumber(record[7]);
				MAX_VALUE	= max(salary, MAX_VALUE);
				MIN_VALUE	= min(salary, MIN_VALUE);
				if (MAX_VALUE >= ULLONG_MAX / 2)
				{
					throw Exception(boost::format("Looks like one of the data points (%1%) is smaller than minus OFFSET (-%2%)") % record[7] % OFFSET);
				}

				LOG(ALL, boost::wformat(L"Salary: %9.2f, data length: %3i") % numberToSalary(salary) % line.size());

				auto oramId	 = PathORAM::hashToNumber(BPlusTree::bytesFromNumber(salary), ORAMS_NUMBER);
				auto blockId = oramsIndex[oramId].size();

				oramsIndex[oramId].push_back({blockId, PathORAM::fromText(line, ORAM_BLOCK_SIZE)});
				treeIndex.push_back({salary, BPlusTree::concatNumbers(2, oramId, blockId)});
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

				MAX_VALUE = max(salary, MAX_VALUE);
				MIN_VALUE = min(salary, MIN_VALUE);

				auto oramId	 = PathORAM::hashToNumber(BPlusTree::bytesFromNumber(salary), ORAMS_NUMBER);
				auto blockId = oramsIndex[oramId].size();

				oramsIndex[oramId].push_back({blockId, PathORAM::fromText(text.str(), ORAM_BLOCK_SIZE)});
				treeIndex.push_back({salary, BPlusTree::concatNumbers(2, oramId, blockId)});
			}

			for (number i = 0; i < SYNTHETIC_QUERIES; i++)
			{
				queries.push_back({salaryToNumber(to_string(8 * i + 3)), salaryToNumber(to_string(8 * i + 8))});
			}
		}

		oramBlockNumbers = transform<vector<pair<number, bytes>>, number>(oramsIndex, [](vector<pair<number, bytes>> oramBlocks) { return oramBlocks.size(); });
		storeInputs(queries, oramBlockNumbers);
	}
	else
	{
		tie(queries, oramBlockNumbers) = loadInputs();
	}

	COUNT = accumulate(oramBlockNumbers.begin(), oramBlockNumbers.end(), 0uLL);

	ORAM_LOG_CAPACITY = ceil(log2(COUNT / ORAMS_NUMBER)) + 1;

	LOG_PARAMETER(COUNT);
	LOG_PARAMETER(GENERATE_INDICES);
	LOG_PARAMETER(READ_INPUTS);
	LOG_PARAMETER(ORAM_BLOCK_SIZE);
	LOG_PARAMETER(ORAM_LOG_CAPACITY);
	LOG_PARAMETER(ORAMS_NUMBER);
	LOG_PARAMETER(PARALLEL);
	LOG_PARAMETER(ORAM_Z);
	LOG_PARAMETER(TREE_BLOCK_SIZE);
	LOG_PARAMETER(USE_ORAMS);
	LOG_PARAMETER(FILE_LOGGING);
	LOG_PARAMETER(VIRTUAL_REQUESTS);
	LOG_PARAMETER(BATCH_SIZE);
	LOG_PARAMETER(SEED);
	LOG_PARAMETER(DP_K);
	LOG_PARAMETER(DP_BETA);
	LOG_PARAMETER(DP_EPSILON);
	LOG_PARAMETER(DP_USE_GAMMA);

	LOG(INFO, boost::wformat(L"ORAM_BACKEND = %1%") % oramBackendStrings[ORAM_STORAGE]);
	LOG(INFO, boost::wformat(L"REDIS_HOST = %1%") % toWString(REDIS_HOST));
	LOG(INFO, boost::wformat(L"AEROSPIKE_HOST = %1%") % toWString(AEROSPIKE_HOST));

#pragma endregion

#pragma region CONSTRUCT_INDICES

	// vector<tuple<elapsed, real, padding, noise, total>>
	using measurement = tuple<number, number, number, number, number>;
	vector<measurement> measurements;

	auto queryIndex = 1;

	if (USE_ORAMS)
	{
		// indices can be empty if generate == false
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

		if (!VIRTUAL_REQUESTS)
		{
			for (auto i = 0; i < ORAMS_NUMBER; i++)
			{
				futures[i] = promises[i].get_future();
				threads[i] = thread(
					loadOram,
					i,
					oramsIndex[i], // may be empty if generate == false
					GENERATE_INDICES,
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
		}
		else
		{
			oramSets.resize(ORAMS_NUMBER);
		}

		auto treeStorage = make_shared<BPlusTree::FileSystemStorageAdapter>(TREE_BLOCK_SIZE, filename(TREE_FILE, -1), GENERATE_INDICES);
		auto tree		 = GENERATE_INDICES ? make_shared<BPlusTree::Tree>(treeStorage, treeIndex) : make_shared<BPlusTree::Tree>(treeStorage);

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
		auto DP_MU	   = optimalMu(1.0 / (1 << DP_BETA), DP_K, DP_BUCKETS, DP_EPSILON, ORAMS_NUMBER);

		LOG_PARAMETER(DP_DOMAIN);
		LOG_PARAMETER(numberToSalary(MIN_VALUE));
		LOG_PARAMETER(numberToSalary(MAX_VALUE));
		LOG_PARAMETER(DP_BUCKETS);
		LOG_PARAMETER(DP_LEVELS);
		LOG_PARAMETER(DP_MU);

		LOG(INFO, L"Generating DP noise tree...");

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

			if (DP_USE_GAMMA)
			{
				// no need for extra noises trees if Gamma is used
				break;
			}
		}

#pragma endregion

#pragma region QUERY

		LOG(INFO, boost::wformat(L"Running %1% queries...") % queries.size());

		// setup Ctrl+C (SIGINT) handler
		struct sigaction sigIntHandler;
		sigIntHandler.sa_handler = [](int s) {
			if (SIGINT_RECEIVED)
			{
				LOG(WARNING, L"Second SIGINT caught. Terminating.");
				exit(1);
			}
			SIGINT_RECEIVED = true;
			LOG(WARNING, L"SIGINT caught. Will stop processing queries. Ctrl+C again to force-terminate.");
		};
		sigemptyset(&sigIntHandler.sa_mask);
		sigIntHandler.sa_flags = 0;
		sigaction(SIGINT, &sigIntHandler, NULL);

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

		for (auto query : queries)
		{
			auto start = chrono::steady_clock::now();

			number realRecordsNumber  = 0;
			number totalRecordsNumber = 0;

			// DP padding
			auto [fromBucket, toBucket, from, to] = padToBuckets(query, MIN_VALUE, MAX_VALUE, DP_BUCKETS);

			auto oramsAndBlocks = tree->search(from, to);

			// DP add noise
			auto noiseNodes = BRC(DP_K, fromBucket, toBucket);

			// add real block IDs
			vector<vector<number>> blockIds;
			blockIds.resize(ORAMS_NUMBER);
			for (auto pair : oramsAndBlocks)
			{
				auto fromTree = BPlusTree::deconstructNumbers(pair);
				auto oramId	  = fromTree[0];
				auto blockId  = fromTree[1];

				blockIds[oramId].push_back(blockId);
			}

			auto totalNoise = 0uLL;
			if (DP_USE_GAMMA)
			{
				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					for (auto node : noiseNodes)
					{
						// real + padded + noise
						totalRecordsNumber += blockIds[i].size() + noises[0][node];
					}
				}
				auto extra = extraGammaNodes(ORAMS_NUMBER, 1.0 / (1 << DP_BETA), totalRecordsNumber);

				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					for (auto j = 0uLL; j < extra; j++)
					{
						blockIds[i].push_back(PathORAM::getRandomUInt(oramBlockNumbers[i]));
					}
				}
				totalNoise = extra * ORAMS_NUMBER;
			}
			else
			{
				// add noisy fake block IDs
				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					for (auto node : noiseNodes)
					{
						for (auto j = 0uLL; j < noises[i][node]; j++)
						{
							blockIds[i].push_back(PathORAM::getRandomUInt(oramBlockNumbers[i]));
						}
						totalNoise += noises[i][node];
					}
				}
			}

			totalRecordsNumber = 0;
			for (auto&& blocks : blockIds)
			{
				totalRecordsNumber += blocks.size();
			}

			LOG(TRACE, boost::wformat(L"Query {%9.2f, %9.2f} was transformed to {%9.2f, %9.2f}, buckets [%4i, %4i], added total of %4i noisy records") % numberToSalary(query.first) % numberToSalary(query.second) % numberToSalary(from) % numberToSalary(to) % fromBucket % toBucket % totalNoise);

			if (!VIRTUAL_REQUESTS)
			{
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
				for (auto record : returned)
				{
					auto text = PathORAM::toText(record, ORAM_BLOCK_SIZE);

					vector<string> broken;
					boost::algorithm::split(broken, text, boost::is_any_of(","));
					auto salary = salaryToNumber(broken[7]);

					if (salary >= query.first && salary <= query.second)
					{
						realRecordsNumber++;
					}
				}
			}
			else
			{
				realRecordsNumber = tree->search(query.first, query.second).size();
			}

			auto paddingRecordsNumber = totalRecordsNumber >= (totalNoise + realRecordsNumber) ? totalRecordsNumber - totalNoise - realRecordsNumber : 0;

			auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();
			measurements.push_back({elapsed, realRecordsNumber, paddingRecordsNumber, totalNoise, totalRecordsNumber});

			LOG(DEBUG, boost::wformat(L"Query %3i / %3i : {%9.2f, %9.2f} the real records %6i ( +%6i padding, +%6i noise, %6i total) (%7s, or %7s / record)") % queryIndex % queries.size() % numberToSalary(query.first) % numberToSalary(query.second) % realRecordsNumber % paddingRecordsNumber % totalNoise % totalRecordsNumber % timeToString(elapsed) % (realRecordsNumber > 0 ? timeToString(elapsed / realRecordsNumber) : L"0 ns"));

			if (SIGINT_RECEIVED)
			{
				LOG(WARNING, L"Stopping query processing due to SIGINT");
				break;
			}

			queryIndex++;
		}

		if (!VIRTUAL_REQUESTS)
		{
			LOG(INFO, L"Saving ORAMs position map and stash to files");
			for (auto i = 0uLL; i < oramSets.size(); i++)
			{
				static_pointer_cast<PathORAM::InMemoryPositionMapAdapter>(get<1>(oramSets[i]))->storeToFile(filename(ORAM_MAP_FILE, i));
				static_pointer_cast<PathORAM::InMemoryStashAdapter>(get<2>(oramSets[i]))->storeToFile(filename(ORAM_STASH_FILE, i));
			}
		}

#pragma endregion
	}
	else
	{
#pragma region STRAWMAN
		bytes storageKey;
		if (GENERATE_INDICES)
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
				storage = make_shared<PathORAM::FileSystemStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, filename(ORAM_STORAGE_FILE, -1), GENERATE_INDICES, 1);
				break;
			case Redis:
				storage = make_shared<PathORAM::RedisStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, redishost(REDIS_HOST, -1), GENERATE_INDICES, 1);
				break;
			case Aerospike:
				storage = make_shared<PathORAM::AerospikeStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, AEROSPIKE_HOST, GENERATE_INDICES, 1);
				break;
		}

		if (GENERATE_INDICES)
		{
			vector<pair<number, vector<pair<number, bytes>>>> batch;
			auto count = 0;
			for (number i = 0; i < ORAMS_NUMBER; i++)
			{
				for (number j = 0; j < oramsIndex[i].size(); j++)
				{
					batch.push_back({count, {oramsIndex[i][j]}});
					count++;
					if (i % BATCH_SIZE == 0 || i == oramsIndex[i].size() - 1)
					{
						if (batch.size() > 0)
						{
							storage->set(batch);
						}
						batch.clear();
					}
				}
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
			measurements.push_back({elapsed, count, 0, 0, COUNT});

			LOG(DEBUG, boost::wformat(L"For query {%9.2f, %9.2f} the result size is %3i (completed in %7s, or %7s per record)") % numberToSalary(query.first) % numberToSalary(query.second) % count % timeToString(elapsed) % (count > 0 ? timeToString(elapsed / count) : L"0 ns"));

			queryIndex++;
		}
#pragma endregion
	}

	LOG(INFO, L"Complete!");

	auto avg = [&measurements](function<number(const measurement&)> getter) -> pair<number, number> {
		auto values = transform<measurement, number>(measurements, getter);
		auto sum	= accumulate(values.begin(), values.end(), 0LL);
		return {sum, sum / values.size()};
	};

	auto [timeTotal, timePerQuery] = avg([](measurement v) { return get<0>(v); });
	auto [realTotal, realPerQuery] = avg([](measurement v) { return get<1>(v); });
	auto paddingPerQuery		   = avg([](measurement v) { return get<2>(v); }).second;
	auto noisePerQuery			   = avg([](measurement v) { return get<3>(v); }).second;
	auto totalPerQuery			   = avg([](measurement v) { return get<4>(v); }).second;

#pragma region WRITE_JSON

	LOG(INFO, boost::wformat(L"For %1% queries: total: %2%, average: %3% per query, %4% per result item; (%5%+%6%+%7%=%8%) records per query") % queryIndex % timeToString(timeTotal) % timeToString(timePerQuery) % timeToString(timeTotal / realTotal) % realPerQuery % paddingPerQuery % noisePerQuery % totalPerQuery);

	wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

	pt::ptree root;
	pt::ptree overheadsNode;

	for (auto measurement : measurements)
	{
		pt::ptree overhead;
		overhead.put("overhead", get<0>(measurement));
		overhead.put("real", get<1>(measurement));
		overhead.put("padding", get<2>(measurement));
		overhead.put("noise", get<3>(measurement));
		overhead.put("total", get<4>(measurement));
		overheadsNode.push_back({"", overhead});
	}

	PUT_PARAMETER(COUNT);
	PUT_PARAMETER(GENERATE_INDICES);
	PUT_PARAMETER(READ_INPUTS);
	PUT_PARAMETER(ORAM_BLOCK_SIZE);
	PUT_PARAMETER(ORAM_LOG_CAPACITY);
	PUT_PARAMETER(ORAMS_NUMBER);
	PUT_PARAMETER(PARALLEL);
	PUT_PARAMETER(ORAM_Z);
	PUT_PARAMETER(TREE_BLOCK_SIZE);
	PUT_PARAMETER(USE_ORAMS);
	PUT_PARAMETER(FILE_LOGGING);
	PUT_PARAMETER(VIRTUAL_REQUESTS);
	PUT_PARAMETER(BATCH_SIZE);
	PUT_PARAMETER(SEED);
	PUT_PARAMETER(DP_BUCKETS);
	PUT_PARAMETER(DP_K);
	PUT_PARAMETER(DP_BETA);
	PUT_PARAMETER(DP_EPSILON);
	PUT_PARAMETER(DP_USE_GAMMA);

	root.put("ORAM_BACKEND", converter.to_bytes(oramBackendStrings[ORAM_STORAGE]));
	root.put("REDIS", REDIS_HOST);
	root.put("AEROSPIKE", AEROSPIKE_HOST);

	root.put("LOG_FILENAME", logName);

	pt::ptree aggregates;
	aggregates.put("timeTotal", timeTotal);
	aggregates.put("timePerQuery", timePerQuery);
	aggregates.put("realTotal", realTotal);
	aggregates.put("realPerQuery", realPerQuery);
	aggregates.put("paddingPerQuery", paddingPerQuery);
	aggregates.put("paddingPerQuery", paddingPerQuery);
	aggregates.put("totalPerQuery", totalPerQuery);
	root.add_child("aggregates", aggregates);

	root.add_child("queries", overheadsNode);

	auto filename = boost::str(boost::format("./results/%1%.json") % logName);

	pt::write_json(filename, root);

	LOG(INFO, boost::wformat(L"Log written to %1%") % converter.from_bytes(filename));

	if (FILE_LOGGING)
	{
		logFile.close();
	}

#pragma endregion

	return 0;
}

#pragma region HELPERS

template <class INPUT, class OUTPUT>
vector<OUTPUT> transform(vector<INPUT>& input, function<OUTPUT(const INPUT&)> application)
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
	auto salaryNumber = (long long)salaryDouble + OFFSET;
	return (number)salaryNumber;
}

double numberToSalary(number salary)
{
	return ((long long)salary - OFFSET) * 0.01;
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

inline void storeInputs(vector<pair<number, number>>& queries, vector<number> oramBlockNumbers)
{
	ofstream statFile(filename(STATS_INPUT_FILE, -1));

	statFile << COUNT << endl;
	statFile << MIN_VALUE << endl;
	statFile << MAX_VALUE << endl;
	for (auto&& blockNumber : oramBlockNumbers)
	{
		statFile << blockNumber << endl;
	}

	statFile.close();

	ofstream queryFile(filename(QUERY_INPUT_FILE, -1));

	for (auto&& query : queries)
	{
		queryFile << query.first << "," << query.second << endl;
	}

	queryFile.close();
}

inline pair<vector<pair<number, number>>, vector<number>> loadInputs()
{
	vector<number> oramBlockNumbers;
	ifstream statFile(filename(STATS_INPUT_FILE, -1));

	statFile >> COUNT >> MIN_VALUE >> MAX_VALUE;
	number blockNumber;
	for (number i = 0; i < ORAMS_NUMBER; i++)
	{
		statFile >> blockNumber;
		oramBlockNumbers.push_back(blockNumber);
	}

	statFile.close();

	vector<pair<number, number>> queries;
	ifstream queryFile(filename(QUERY_INPUT_FILE, -1));

	string line = "";
	while (getline(queryFile, line))
	{
		vector<string> query;
		boost::algorithm::split(query, line, boost::is_any_of(","));
		auto left  = stoull(query[0]);
		auto right = stoull(query[1]);

		queries.push_back({left, right});
	}
	queryFile.close();

	return {queries, oramBlockNumbers};
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
		wcout << L"[" << put_time(localtime(&t), L"%d/%m/%Y %H:%M:%S") << L"] " << setw(8) << logLevelColors[level] << logLevelStrings[level] << L": " << message << RESET << endl;

		if (FILE_LOGGING)
		{
			wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			logFile << "[" << put_time(localtime(&t), "%d/%m/%Y %H:%M:%S") << "] " << setw(8) << converter.to_bytes(logLevelStrings[level]) << ": " << converter.to_bytes(message) << endl;
		}
	}
}

#pragma endregion
