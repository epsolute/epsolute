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
#include <rpc/client.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <thread>

using namespace std;
using namespace DPORAM;

namespace po = boost::program_options;
namespace pt = boost::property_tree;

using profile = tuple<bool, number, number, number>;

string filename(string filename, int i);
template <class INPUT, class OUTPUT>
vector<OUTPUT> transform(const vector<INPUT>& input, function<OUTPUT(const INPUT&)> application);
wstring toWString(string input);

inline void storeInputs(vector<pair<number, number>>& queries, vector<number>& oramBlockNumbers);
inline void loadInputs(vector<pair<number, number>>& queries, vector<number>& oramBlockNumbers);

void addFakeRequests(vector<number>& blocks, number maxBlocks, number fakesNumber);
void printProfileStats(vector<profile>& profiles, number queries = 0);
void dumpToMattermost(int argc, char* argv[]);

void LOG(LOG_LEVEL level, wstring message);
void LOG(LOG_LEVEL level, boost::wformat message);

#pragma region GLOBALS

auto COUNT					  = 1000uLL;
auto ORAM_BLOCK_SIZE		  = 256uLL;
auto ORAM_LOG_CAPACITY		  = 10uLL;
auto ORAMS_NUMBER			  = 1uLL;
auto PARALLEL				  = true;
auto ORAM_Z					  = 3uLL;
const auto TREE_BLOCK_SIZE	  = 64uLL;
auto ORAM_STORAGE			  = FileSystem;
auto PROFILE_STORAGE_REQUESTS = false;
auto PROFILE_THREADS		  = false;
auto USE_ORAMS				  = true;
auto USE_ORAM_OPTIMIZATION	  = true;
auto VIRTUAL_REQUESTS		  = false;
const auto BATCH_SIZE		  = 1000;
auto SYNTHETIC_QUERIES		  = 20uLL;

vector<string> RPC_HOSTS;

auto READ_INPUTS	  = true;
auto GENERATE_INDICES = true;
auto DATASET_TAG	  = string("270K-1.7M-uniform");
auto QUERYSET_TAG	  = string("270K-1.7M-uniform-500");

auto DISABLE_ENCRYPTION = false;

auto DP_K		  = 16uLL;
auto DP_BETA	  = 10uLL;
auto DP_EPSILON	  = 10uLL;
auto DP_BUCKETS	  = 0uLL;
auto DP_USE_GAMMA = false;
auto DP_LEVELS	  = 100uLL;

auto SEED = 1305;

number MIN_VALUE = ULONG_MAX;
number MAX_VALUE = 0;
number MAX_RANGE = 0;

const auto FILES_DIR		 = "./storage-files";
const auto KEY_FILE			 = "key";
const auto TREE_FILE		 = "tree";
const auto ORAM_STORAGE_FILE = "oram-storage";
const auto ORAM_MAP_FILE	 = "oram-map";
const auto ORAM_STASH_FILE	 = "oram-stash";
const auto STATS_INPUT_FILE	 = "stats-input";
const auto QUERY_INPUT_FILE	 = "query-input";

vector<string> REDIS_HOSTS;
auto REDIS_FLUSH_ALL = false;

const auto INPUT_FILES_DIR = string("../../experiments-scripts/scripts/");

auto DUMP_TO_MATTERMOST = true;
vector<string> logLines;

auto FILE_LOGGING = false;
string logName;
ofstream logFile;

auto SIGINT_RECEIVED = false;

mutex profileMutex;

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
	auto recordSizeCheck	= [](number v) { if (v < 256uLL) { throw Exception("--recordSize too small"); } };
	auto epsilonCheck		= [](number v) { if (v == 0 ) { throw Exception("malformed --epsilon, mist be >= 1"); } };
	auto betaCheck			= [](number v) { if (v < 1) { throw Exception("malformed --beta, must be >= 1"); } };
	auto bucketsNumberCheck = [](int v) {
		auto logV = log(v) / log(DP_K);
		if (ceil(logV) != floor(logV))
		{
			throw Exception(boost::format("malformed --bucketsNumber, must be a power of %1%") % DP_K);
		}
	};

	po::options_description desc("range query processor", 120);
	desc.add_options()("help,h", "produce help message");
	desc.add_options()("generateIndices,g", po::value<bool>(&GENERATE_INDICES)->default_value(GENERATE_INDICES), "if set, will generate ORAM and tree indices, otherwise will read files");
	desc.add_options()("readInputs,r", po::value<bool>(&READ_INPUTS)->default_value(READ_INPUTS), "if set, will read inputs from files");
	desc.add_options()("parallel,p", po::value<bool>(&PARALLEL)->default_value(PARALLEL), "if set, will query orams in parallel");
	desc.add_options()("oramStorage,s", po::value<ORAM_BACKEND>(&ORAM_STORAGE)->default_value(ORAM_STORAGE), "the ORAM backend to use");
	desc.add_options()("oramsNumber,n", po::value<number>(&ORAMS_NUMBER)->notifier(oramsNumberCheck)->default_value(ORAMS_NUMBER), "the number of parallel ORAMs to use");
	desc.add_options()("recordSize", po::value<number>(&ORAM_BLOCK_SIZE)->notifier(recordSizeCheck)->default_value(ORAM_BLOCK_SIZE), "the record size in bytes");
	desc.add_options()("oramsZ,z", po::value<number>(&ORAM_Z)->default_value(ORAM_Z), "the Z parameter for ORAMs");
	desc.add_options()("bucketsNumber,b", po::value<number>(&DP_BUCKETS)->notifier(bucketsNumberCheck)->default_value(DP_BUCKETS), "the number of buckets for DP (if 0, will choose max buckets such that less than the domain size)");
	desc.add_options()("useOrams,u", po::value<bool>(&USE_ORAMS)->default_value(USE_ORAMS), "if set will use ORAMs, otherwise each query will download everything every query");
	desc.add_options()("useOramOptimization", po::value<bool>(&USE_ORAM_OPTIMIZATION)->default_value(USE_ORAM_OPTIMIZATION), "if set will use ORAM batch processing");
	desc.add_options()("rpcHost", po::value<vector<string>>(&RPC_HOSTS)->multitoken()->composing(), "If set, will use these hosts in RPC setting; will uniformly distribute ORAMs among these hosts; may optionally include port (e.g. 127.0.0.1:8787);");
	desc.add_options()("dataset", po::value<string>(&DATASET_TAG)->default_value(DATASET_TAG), "the dataset tag to use when reading dataset file");
	desc.add_options()("queryset", po::value<string>(&QUERYSET_TAG)->default_value(QUERYSET_TAG), "the queryset tag to use when reading queryset file");
	desc.add_options()("profileStorage", po::value<bool>(&PROFILE_STORAGE_REQUESTS)->default_value(PROFILE_STORAGE_REQUESTS), "if set, will listen to storage events and record them");
	desc.add_options()("profileThreads", po::value<bool>(&PROFILE_THREADS)->default_value(PROFILE_THREADS), "if set, will log additional data on threads performance");
	desc.add_options()("virtualRequests", po::value<bool>(&VIRTUAL_REQUESTS)->default_value(VIRTUAL_REQUESTS), "if set, will only simulate ORAM queries, not actually make them");
	desc.add_options()("beta", po::value<number>(&DP_BETA)->notifier(betaCheck)->default_value(DP_BETA), "beta parameter for DP; x such that beta = 2^{-x}");
	desc.add_options()("epsilon", po::value<number>(&DP_EPSILON)->notifier(epsilonCheck)->default_value(DP_EPSILON), "epsilon parameter for DP");
	desc.add_options()("useGamma", po::value<bool>(&DP_USE_GAMMA)->default_value(DP_USE_GAMMA), "if set, will use Gamma method to add noise per ORAM");
	desc.add_options()("levels", po::value<number>(&DP_LEVELS)->default_value(DP_LEVELS), "number of levels to keep in DP tree (0 for choosing optimal for given queries)");
	desc.add_options()("count", po::value<number>(&COUNT)->default_value(COUNT), "number of synthetic records to generate");
	desc.add_options()("queries", po::value<number>(&SYNTHETIC_QUERIES)->default_value(SYNTHETIC_QUERIES), "number of synthetic queries to generate");
	desc.add_options()("fanout,k", po::value<number>(&DP_K)->default_value(DP_K), "DP tree fanout");
	desc.add_options()("verbosity,v", po::value<LOG_LEVEL>(&__logLevel)->default_value(INFO), "verbosity level to output");
	desc.add_options()("fileLogging", po::value<bool>(&FILE_LOGGING)->default_value(FILE_LOGGING), "if set, log stream will be duplicated to file (noticeably slows down simulation)");
	desc.add_options()("disableEncryption", po::value<bool>(&DISABLE_ENCRYPTION)->default_value(DISABLE_ENCRYPTION), "if set, will disable encryption in ORAM");
	desc.add_options()("dumpToMattermost", po::value<bool>(&DUMP_TO_MATTERMOST)->default_value(DUMP_TO_MATTERMOST), "if set, will dump log to mattermost");
	desc.add_options()("redisFlushAll", po::value<bool>(&REDIS_FLUSH_ALL)->default_value(REDIS_FLUSH_ALL), "if set, will execute FLUSHALL for all supplied redis hosts");
	desc.add_options()("redis", po::value<vector<string>>(&REDIS_HOSTS)->multitoken()->composing(), "Redis host(s) to use. If multiple specified, will distribute uniformly. Default tcp://127.0.0.1:6379 .");
	desc.add_options()("seed", po::value<int>(&SEED)->default_value(SEED), "To use if in DEBUG mode (otherwise OpenSSL will sample fresh randomness)");

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

	if (ORAM_STORAGE == FileSystem && !USE_ORAMS && PARALLEL)
	{
		LOG(WARNING, L"Can't use FS strawman storage in parallel. PARALLEL will be set to false.");
		PARALLEL = false;
	}

	if (ORAMS_NUMBER == 1 && PARALLEL)
	{
		LOG(WARNING, L"Parallel execution is pointless when ORAMS_NUMBER is 1. PARALLEL will be set to false.");
		PARALLEL = false;
	}

	if (DISABLE_ENCRYPTION)
	{
		LOG(WARNING, L"Encryption disabled");
		PathORAM::__blockCipherMode = PathORAM::BlockCipherMode::NONE;
	}

	if (ORAM_STORAGE != Redis && RPC_HOSTS.size() > 0)
	{
		LOG(WARNING, L"RPC requires Redis storage. ORAM_STORAGE will be set to Redis.");
		ORAM_STORAGE = Redis;
	}

	if (!PARALLEL && RPC_HOSTS.size() > 0)
	{
		LOG(WARNING, L"RPC requires parallel execution. PARALLEL will be set to true.");
		PARALLEL = true;
	}

	if (VIRTUAL_REQUESTS && RPC_HOSTS.size() > 0)
	{
		LOG(WARNING, L"RPC does not work with virtual requests. VIRTUAL_REQUESTS will be set to false.");
		VIRTUAL_REQUESTS = false;
	}

	if (PROFILE_STORAGE_REQUESTS && RPC_HOSTS.size() > 0)
	{
		LOG(WARNING, L"RPC does not work with profiling. PROFILE_STORAGE_REQUESTS will be set to false.");
		PROFILE_STORAGE_REQUESTS = false;
	}

	if (REDIS_HOSTS.size() == 0)
	{
		REDIS_HOSTS.push_back("tcp://127.0.0.1:6379");
	}

	if (REDIS_FLUSH_ALL)
	{
		LOG(WARNING, L"REDIS_FLUSH_ALL is set, will delete all databases in supplied hosts.");
		for (auto&& redisHost : REDIS_HOSTS)
		{
			sw::redis::Redis(redisHost).flushall();
		}
	}

	vector<unique_ptr<rpc::client>> rpcClients;
	vector<number> oramToRpcMap;
	for (auto&& rpcHost : RPC_HOSTS)
	{
		if (rpcHost.find(':') != string::npos)
		{
			vector<string> pieces;
			boost::algorithm::split(pieces, rpcHost, boost::is_any_of(":"));
			rpcClients.push_back(make_unique<rpc::client>(pieces[0], stoi(pieces[1])));
		}
		else
		{
			rpcClients.push_back(make_unique<rpc::client>(rpcHost, RPC_PORT));
		}
	}
	if (RPC_HOSTS.size() > 0)
	{
		for (auto i = 0u; i < ORAMS_NUMBER; i++)
		{
			oramToRpcMap.push_back(i % RPC_HOSTS.size());
		}
		for (auto&& rpcClient : rpcClients)
		{
			rpcClient->call("reset");
		}
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
			auto dataFilePath = (boost::filesystem::path(INPUT_FILES_DIR) / (DATASET_TAG + ".csv")).string();
			ifstream dataFile(dataFilePath);
			if (!dataFile.is_open())
			{
				LOG(CRITICAL, boost::wformat(L"File cannot be opened: %s") % toWString(dataFilePath));
			}

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

				auto toHash	 = BPlusTree::bytesFromNumber(salary);
				auto oramId	 = PathORAM::hashToNumber(toHash, ORAMS_NUMBER);
				auto blockId = oramsIndex[oramId].size();

				oramsIndex[oramId].push_back({blockId, PathORAM::fromText(line, ORAM_BLOCK_SIZE)});
				treeIndex.push_back({salary, BPlusTree::concatNumbers(2, oramId, blockId)});
			}
			dataFile.close();

			auto queryFilePath = (boost::filesystem::path(INPUT_FILES_DIR) / (QUERYSET_TAG + ".csv")).string();
			ifstream queryFile(queryFilePath);
			if (!queryFile.is_open())
			{
				LOG(CRITICAL, boost::wformat(L"File cannot be opened: %s") % toWString(queryFilePath));
			}

			line = "";
			while (getline(queryFile, line))
			{
				vector<string> query;
				boost::algorithm::split(query, line, boost::is_any_of(","));
				auto left  = salaryToNumber(query[0]);
				auto right = salaryToNumber(query[1]);

				LOG(ALL, boost::wformat(L"Query: {%9.2f, %9.2f}") % numberToSalary(left) % numberToSalary(right));

				queries.push_back({left, right});

				MAX_RANGE = max(MAX_RANGE, (right - left) / 100);
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

				auto toHash	 = BPlusTree::bytesFromNumber(salary);
				auto oramId	 = PathORAM::hashToNumber(toHash, ORAMS_NUMBER);
				auto blockId = oramsIndex[oramId].size();

				oramsIndex[oramId].push_back({blockId, PathORAM::fromText(text.str(), ORAM_BLOCK_SIZE)});
				treeIndex.push_back({salary, BPlusTree::concatNumbers(2, oramId, blockId)});
			}

			for (number i = 0; i < SYNTHETIC_QUERIES; i++)
			{
				auto left  = salaryToNumber(to_string(8 * i + 3));
				auto right = salaryToNumber(to_string(8 * i + 8));
				queries.push_back({left, right});
				MAX_RANGE = max(MAX_RANGE, (right - left) / 100);
			}
		}

		oramBlockNumbers = transform<vector<pair<number, bytes>>, number>(oramsIndex, [](const vector<pair<number, bytes>>& oramBlocks) { return oramBlocks.size(); });
		storeInputs(queries, oramBlockNumbers);
	}
	else
	{
		oramBlockNumbers.clear();
		loadInputs(queries, oramBlockNumbers);
	}

	COUNT = accumulate(oramBlockNumbers.begin(), oramBlockNumbers.end(), 0uLL);

	ORAM_LOG_CAPACITY = ceil(log2(COUNT / ORAMS_NUMBER / ORAM_Z)) + 1;

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
	LOG_PARAMETER(USE_ORAM_OPTIMIZATION);
	for (auto&& rpcHost : RPC_HOSTS)
	{
		LOG_PARAMETER(toWString(rpcHost));
	}
	LOG_PARAMETER(DISABLE_ENCRYPTION);
	LOG_PARAMETER(PROFILE_STORAGE_REQUESTS);
	LOG_PARAMETER(PROFILE_THREADS);
	LOG_PARAMETER(REDIS_FLUSH_ALL);
	LOG_PARAMETER(FILE_LOGGING);
	LOG_PARAMETER(DUMP_TO_MATTERMOST);
	LOG_PARAMETER(VIRTUAL_REQUESTS);
	LOG_PARAMETER(BATCH_SIZE);
	LOG_PARAMETER(SEED);
	LOG_PARAMETER(DP_K);
	LOG_PARAMETER(DP_BETA);
	LOG_PARAMETER(DP_EPSILON);
	LOG_PARAMETER(DP_USE_GAMMA);

	LOG(INFO, boost::wformat(L"DATASET_TAG = %1%") % toWString(DATASET_TAG));
	LOG(INFO, boost::wformat(L"QUERYSET_TAG = %1%") % toWString(QUERYSET_TAG));

	LOG(INFO, boost::wformat(L"ORAM_BACKEND = %1%") % oramBackendStrings[ORAM_STORAGE]);
	for (auto&& redisHost : REDIS_HOSTS)
	{
		LOG_PARAMETER(toWString(redisHost));
	}

#pragma endregion

#pragma region CONSTRUCT_INDICES

	// vector<tuple<elapsed, real, padding, noise, total>>
	using measurement = tuple<number, number, number, number, number>;
	vector<measurement> measurements;

	vector<profile> profiles;
	vector<profile> allProfiles;

	auto queryIndex = 1;

	if (USE_ORAMS)
	{
		LOG(INFO, L"Loading ORAMs and B+ tree");

		// indices can be empty if generate == false
		auto loadOram = [&profiles, &allProfiles](int i, vector<pair<number, bytes>> indices, bool generate, string redisHost, promise<ORAMSet>* promise) -> void {
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
					oramStorage = make_shared<PathORAM::InMemoryStorageAdapter>((1 << ORAM_LOG_CAPACITY) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, ORAM_Z);
					break;
				case FileSystem:
					oramStorage = make_shared<PathORAM::FileSystemStorageAdapter>((1 << ORAM_LOG_CAPACITY) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, filename(ORAM_STORAGE_FILE, i), generate, ORAM_Z);
					break;
				case Redis:
					oramStorage = make_shared<PathORAM::RedisStorageAdapter>((1 << ORAM_LOG_CAPACITY) + ORAM_Z, ORAM_BLOCK_SIZE, oramKey, redishost(redisHost, i), generate, ORAM_Z);
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

			if (PROFILE_STORAGE_REQUESTS)
			{
				oramStorage->subscribe([&profiles, &allProfiles](bool read, number batch, number size, number overhead) -> void {
					lock_guard<mutex> guard(profileMutex);

					profiles.push_back({read, batch, size, overhead});
					allProfiles.push_back({read, batch, size, overhead});
				});
			}

			promise->set_value({oramStorage, oramPositionMap, oramStash, oram});
		};

		vector<ORAMSet> oramSets;
		thread threads[ORAMS_NUMBER];
		promise<ORAMSet> promises[ORAMS_NUMBER];
		future<ORAMSet> futures[ORAMS_NUMBER];

		if (!VIRTUAL_REQUESTS && rpcClients.size() == 0)
		{
			for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
			{
				futures[i] = promises[i].get_future();
				threads[i] = thread(
					loadOram,
					i,
					oramsIndex[i], // may be empty if generate == false
					GENERATE_INDICES,
					REDIS_HOSTS[i % REDIS_HOSTS.size()],
					&promises[i]);
			}

			for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
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

		if (rpcClients.size() > 0)
		{
			thread threads[ORAMS_NUMBER];

			for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
			{
				threads[i] = thread(
					[&rpcClients, &oramsIndex, &oramToRpcMap](number oramId) -> void {
						rpcClients[oramToRpcMap[oramId]]->call("setOram", oramId, REDIS_HOSTS[oramId % REDIS_HOSTS.size()], oramsIndex[oramId], ORAM_LOG_CAPACITY, ORAM_BLOCK_SIZE, ORAM_Z);
					},
					i);
			}

			for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
			{
				threads[i].join();
			}
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

		if (DP_LEVELS == 0)
		{
			auto maxBuckets = (MAX_RANGE * DP_BUCKETS + DP_DOMAIN - 1) / DP_DOMAIN;
			DP_LEVELS		= max((number)ceil(log(maxBuckets) / log(DP_K)), 1uLL);
			LOG(INFO, boost::wformat(L"DP_LEVELS is optimally set at %1%, given that biggest query will span at most %2% buckets") % DP_LEVELS % maxBuckets);
		}
		else
		{
			auto maxLevels = (number)(log(DP_BUCKETS) / log(DP_K));
			if (DP_LEVELS > maxLevels)
			{
				DP_LEVELS = maxLevels;
			}
		}

		auto DP_MU = optimalMu(1.0 / (1 << DP_BETA), DP_K, DP_BUCKETS, DP_EPSILON, DP_LEVELS, DP_USE_GAMMA ? 1 : ORAMS_NUMBER);

		LOG_PARAMETER(DP_DOMAIN);
		LOG_PARAMETER(numberToSalary(MIN_VALUE));
		LOG_PARAMETER(numberToSalary(MAX_VALUE));
		LOG_PARAMETER(MAX_RANGE);
		LOG_PARAMETER(DP_BUCKETS);
		LOG_PARAMETER(DP_LEVELS);
		LOG_PARAMETER(DP_MU);

		LOG(INFO, L"Generating DP noise tree...");

		vector<map<pair<number, number>, number>> noises;
		noises.resize(ORAMS_NUMBER);

		for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
		{
			auto buckets = DP_BUCKETS;
			for (auto l = 0uLL; l < DP_LEVELS; l++)
			{
				for (auto j = 0uLL; j < buckets; j++)
				{
					noises[i][{l, j}] = (int)sampleLaplace(DP_MU, (double)DP_LEVELS / DP_EPSILON);
				}
				buckets /= DP_K;
			}

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

		// returns tuple<# real records, thread overhead, # of processed requests>
		using queryReturnType = tuple<number, chrono::steady_clock::rep, number>;
		using rpcReturnType	  = vector<tuple<vector<bytes>, chrono::steady_clock::rep, number>>;

		auto queryRpc = [&rpcClients](number rpcClientId, const vector<pair<number, vector<number>>>& ids, pair<number, number> query, promise<rpcReturnType>* promise) -> void {
			auto result = rpcClients[rpcClientId]->call("runQuery", ids, query).as<rpcReturnType>();
			promise->set_value(result);
		};

		auto queryOram = [](const vector<number>& ids, shared_ptr<PathORAM::ORAM> oram, number from, number to, promise<queryReturnType>* promise) -> queryReturnType {
			vector<bytes> answer;
			number count = 0;

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
							count++;
						}
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
						count++;
					}
				}
			}

			auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();

			if (promise != NULL)
			{
				promise->set_value({count, elapsed, ids.size()});
			}

			return {count, elapsed, ids.size()};
		};

		for (auto query : queries)
		{
			auto start = chrono::steady_clock::now();

			if (PROFILE_STORAGE_REQUESTS)
			{
				profiles.clear();
			}

			number realRecordsNumber  = 0;
			number totalRecordsNumber = 0;

			// DP padding
			auto [fromBucket, toBucket, from, to] = padToBuckets(query, MIN_VALUE, MAX_VALUE, DP_BUCKETS);

			vector<bytes> oramsAndBlocks;
			tree->search(from, to, oramsAndBlocks);

			// DP add noise
			auto noiseNodes = BRC(DP_K, fromBucket, toBucket);
			for (auto node : noiseNodes)
			{
				if (node.first >= DP_LEVELS)
				{
					LOG(CRITICAL, boost::wformat(L"DP tree is not high enough. Level %1% is not generated.") % node.first);
				}
			}

			// add real block IDs
			vector<vector<number>> blockIds;
			blockIds.resize(ORAMS_NUMBER);
			for (auto&& pair : oramsAndBlocks)
			{
				auto fromTree = BPlusTree::deconstructNumbers(pair);
				auto oramId	  = fromTree[0];
				auto blockId  = fromTree[1];

				blockIds[oramId].push_back(blockId);
			}

			auto totalNoise = 0uLL;
			if (DP_USE_GAMMA)
			{
				auto kZeroTilda = oramsAndBlocks.size();
				for (auto node : noiseNodes)
				{
					kZeroTilda += noises[0][node];
				}
				auto maxRecords = gammaNodes(ORAMS_NUMBER, 1.0 / (1 << DP_BETA), kZeroTilda);

				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					auto extra = blockIds[i].size() < maxRecords ? maxRecords - blockIds[i].size() : 0;
					addFakeRequests(blockIds[i], oramBlockNumbers[i], extra);
					totalNoise += extra;
				}
			}
			else
			{
				// add noisy fake block IDs
				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					for (auto node : noiseNodes)
					{
						addFakeRequests(blockIds[i], oramBlockNumbers[i], noises[i][node]);
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

			chrono::steady_clock::time_point timestampBeforeORAMs;
			chrono::steady_clock::time_point timestampAfterORAMs;

			if (!VIRTUAL_REQUESTS)
			{
				vector<chrono::steady_clock::rep> threadOverheads;
				vector<number> threadAnswerSizes;

				if (RPC_HOSTS.size() > 0)
				{
					thread threads[RPC_HOSTS.size()];
					promise<rpcReturnType> promises[RPC_HOSTS.size()];
					future<rpcReturnType> futures[RPC_HOSTS.size()];

					timestampBeforeORAMs = chrono::steady_clock::now();

					for (auto rpcHostId = 0uLL; rpcHostId < RPC_HOSTS.size(); rpcHostId++)
					{
						vector<pair<number, vector<number>>> ids;
						for (auto oramId = 0uLL; oramId < oramToRpcMap.size(); oramId++)
						{
							if (oramToRpcMap[oramId] == rpcHostId)
							{
								ids.push_back({oramId, blockIds[oramId]});
							}
						}

						futures[rpcHostId] = promises[rpcHostId].get_future();
						threads[rpcHostId] = thread(queryRpc, rpcHostId, ids, query, &promises[rpcHostId]);
					}

					for (auto i = 0uLL; i < RPC_HOSTS.size(); i++)
					{
						auto returned = futures[i].get();
						for (auto&& threadRunResult : returned)
						{
							realRecordsNumber += get<0>(threadRunResult).size();
							threadOverheads.push_back(get<1>(threadRunResult));
							threadAnswerSizes.push_back(get<2>(threadRunResult));
						}
						threads[i].join();
					}

					timestampAfterORAMs = chrono::steady_clock::now();
				}
				else if (PARALLEL)
				{
					thread threads[ORAMS_NUMBER];
					promise<queryReturnType> promises[ORAMS_NUMBER];
					future<queryReturnType> futures[ORAMS_NUMBER];

					timestampBeforeORAMs = chrono::steady_clock::now();

					for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
					{
						futures[i] = promises[i].get_future();
						threads[i] = thread(queryOram, blockIds[i], orams[i], query.first, query.second, &promises[i]);
					}

					for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
					{
						auto returned = futures[i].get();
						realRecordsNumber += get<0>(returned);
						threadOverheads.push_back(get<1>(returned));
						threadAnswerSizes.push_back(get<2>(returned));
						threads[i].join();
					}

					timestampAfterORAMs = chrono::steady_clock::now();
				}
				else
				{
					timestampBeforeORAMs = chrono::steady_clock::now();

					for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
					{
						auto returned = queryOram(blockIds[i], orams[i], query.first, query.second, NULL);
						realRecordsNumber += get<0>(returned);
						threadOverheads.push_back(get<1>(returned));
						threadAnswerSizes.push_back(get<2>(returned));
					}

					timestampAfterORAMs = chrono::steady_clock::now();
				}

				auto queryOverheadBefore = chrono::duration_cast<chrono::nanoseconds>(timestampBeforeORAMs - start).count();
				auto queryOverheadORAMs	 = chrono::duration_cast<chrono::nanoseconds>(timestampAfterORAMs - timestampBeforeORAMs).count();
				auto queryOverheadAfter	 = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - timestampAfterORAMs).count();

				auto threadOverheadsMinIndex = min_element(threadOverheads.begin(), threadOverheads.end()) - threadOverheads.begin();
				auto threadOverheadsMaxIndex = max_element(threadOverheads.begin(), threadOverheads.end()) - threadOverheads.begin();
				auto threadOverheadsMin		 = threadOverheads[threadOverheadsMinIndex];
				auto threadAnswersizeMin	 = threadAnswerSizes[threadOverheadsMinIndex];
				auto threadOverheadsMax		 = threadOverheads[threadOverheadsMaxIndex];
				auto threadAnswersizeMax	 = threadAnswerSizes[threadOverheadsMaxIndex];

				auto threadOverheadsSum		  = accumulate(threadOverheads.begin(), threadOverheads.end(), 0uLL);
				auto threadOverheadsMean	  = threadOverheadsSum / threadOverheads.size();
				auto threadOverheadsSquareSum = inner_product(threadOverheads.begin(), threadOverheads.end(), threadOverheads.begin(), 0uLL);
				auto threadOverheadsStdDev	  = sqrt(threadOverheadsSquareSum / threadOverheads.size() - threadOverheadsMean * threadOverheadsMean);

				LOG(TRACE, boost::wformat(L"Query: {before: %7s, ORAMs: %7s, after: %7s}, threads: {min: %7s (%4i), max: %7s (%4i), avg: %7s, stddev: %7s}") % timeToString(queryOverheadBefore) % timeToString(queryOverheadORAMs) % timeToString(queryOverheadAfter) % timeToString(threadOverheadsMin) % threadAnswersizeMin % timeToString(threadOverheadsMax) % threadAnswersizeMax % timeToString(threadOverheadsMean) % timeToString(threadOverheadsStdDev));
				if (PROFILE_THREADS)
				{
					wstringstream wss;
					wss << L"Threads: [ ";
					for (auto i = 0u; i < ORAMS_NUMBER; i++)
					{
						wss << timeToString(threadOverheads[i]);
						if (i != ORAMS_NUMBER - 1)
						{
							wss << ", ";
						}
					}
					wss << L" ]";
					LOG(TRACE, wss.str());
				}
			}
			else
			{
				vector<bytes> result;
				tree->search(query.first, query.second, result);
				realRecordsNumber = result.size();
			}

			auto paddingRecordsNumber = totalRecordsNumber >= (totalNoise + realRecordsNumber) ? totalRecordsNumber - totalNoise - realRecordsNumber : 0;

			auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();
			measurements.push_back({elapsed, realRecordsNumber, paddingRecordsNumber, totalNoise, totalRecordsNumber});

			LOG(DEBUG, boost::wformat(L"Query %3i / %3i : {%9.2f, %9.2f} the real records %6i ( +%6i padding, +%6i noise, %6i total) (%7s, or %7s / record)") % queryIndex % queries.size() % numberToSalary(query.first) % numberToSalary(query.second) % realRecordsNumber % paddingRecordsNumber % totalNoise % totalRecordsNumber % timeToString(elapsed) % (realRecordsNumber > 0 ? timeToString(elapsed / realRecordsNumber) : L"0 ns"));

			if (PROFILE_STORAGE_REQUESTS)
			{
				printProfileStats(profiles);
			}

			if (SIGINT_RECEIVED)
			{
				LOG(WARNING, L"Stopping query processing due to SIGINT");
				break;
			}

			queryIndex++;
		}

		if (!VIRTUAL_REQUESTS && rpcClients.size() == 0)
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
				storage = make_shared<PathORAM::FileSystemStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, filename(ORAM_STORAGE_FILE, -1), false, 1);
				break;
			case Redis:
				storage = make_shared<PathORAM::RedisStorageAdapter>(COUNT, ORAM_BLOCK_SIZE, storageKey, redishost(REDIS_HOSTS[0], -1), false, 1);
				break;
		}

		if (GENERATE_INDICES)
		{
			LOG(INFO, L"Uploading strawman dataset");

			vector<pair<const number, vector<pair<number, bytes>>>> batch;
			auto count = 0;
			for (number i = 0; i < ORAMS_NUMBER; i++)
			{
				for (number j = 0; j < oramsIndex[i].size(); j++)
				{
					batch.push_back({count, {oramsIndex[i][j]}});
					count++;
					if (j % BATCH_SIZE == 0 || j == oramsIndex[i].size() - 1)
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
						vector<PathORAM::block> returned;
						storage->get(batch, returned);
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

		LOG(INFO, L"Running strawman queries");

		for (auto query : queries)
		{
			auto start = chrono::steady_clock::now();

			auto count = 0;
			if (PARALLEL)
			{
				thread threads[ORAMS_NUMBER];
				promise<vector<string>> promises[ORAMS_NUMBER];
				future<vector<string>> futures[ORAMS_NUMBER];

				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					futures[i] = promises[i].get_future();
					threads[i] = thread(storageQuery, i * COUNT / ORAMS_NUMBER, (i + 1) * COUNT / ORAMS_NUMBER, query.first, query.second, &promises[i]);
				}

				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					auto result = futures[i].get();
					threads[i].join();
					count += result.size();
				}
			}
			else
			{
				for (auto i = 0uLL; i < ORAMS_NUMBER; i++)
				{
					auto result = storageQuery(i * COUNT / ORAMS_NUMBER, (i + 1) * COUNT / ORAMS_NUMBER, query.first, query.second, NULL);
					count += result.size();
				}
			}

			auto elapsed = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - start).count();
			measurements.push_back({elapsed, count, 0, COUNT - count, COUNT});

			LOG(DEBUG, boost::wformat(L"Query %3i / %3i : {%9.2f, %9.2f} the result size is %3i (completed in %7s, or %7s per record)") % queryIndex % queries.size() % numberToSalary(query.first) % numberToSalary(query.second) % count % timeToString(elapsed) % (count > 0 ? timeToString(elapsed / count) : L"0 ns"));

			queryIndex++;
		}
#pragma endregion
	}

	LOG(INFO, L"Complete!");

	for (auto&& rpcClient : rpcClients)
	{
		rpcClient->call("reset");
	}

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

	LOG(INFO, boost::wformat(L"For %1% queries: total: %2%, average: %3% per query, %4% per result item; (%5%+%6%+%7%=%8%) records per query") % (queryIndex - 1) % timeToString(timeTotal) % timeToString(timePerQuery) % timeToString(timeTotal / realTotal) % realPerQuery % paddingPerQuery % noisePerQuery % totalPerQuery);
	if (PROFILE_STORAGE_REQUESTS)
	{
		printProfileStats(allProfiles, queryIndex - 1);
	}

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
	PUT_PARAMETER(DATASET_TAG);
	PUT_PARAMETER(QUERYSET_TAG);
	PUT_PARAMETER(GENERATE_INDICES);
	PUT_PARAMETER(READ_INPUTS);
	PUT_PARAMETER(ORAM_BLOCK_SIZE);
	PUT_PARAMETER(ORAM_LOG_CAPACITY);
	PUT_PARAMETER(ORAMS_NUMBER);
	PUT_PARAMETER(PARALLEL);
	PUT_PARAMETER(ORAM_Z);
	PUT_PARAMETER(TREE_BLOCK_SIZE);
	PUT_PARAMETER(USE_ORAMS);
	PUT_PARAMETER(USE_ORAM_OPTIMIZATION);
	for (auto&& rpcHost : RPC_HOSTS)
	{
		PUT_PARAMETER(rpcHost);
	}
	PUT_PARAMETER(DISABLE_ENCRYPTION);
	PUT_PARAMETER(PROFILE_STORAGE_REQUESTS);
	PUT_PARAMETER(PROFILE_THREADS);
	PUT_PARAMETER(REDIS_FLUSH_ALL);
	PUT_PARAMETER(FILE_LOGGING);
	PUT_PARAMETER(DUMP_TO_MATTERMOST);
	PUT_PARAMETER(VIRTUAL_REQUESTS);
	PUT_PARAMETER(BATCH_SIZE);
	PUT_PARAMETER(SEED);
	PUT_PARAMETER(DP_BUCKETS);
	PUT_PARAMETER(DP_K);
	PUT_PARAMETER(DP_BETA);
	PUT_PARAMETER(DP_EPSILON);
	PUT_PARAMETER(DP_USE_GAMMA);

	root.put("ORAM_BACKEND", converter.to_bytes(oramBackendStrings[ORAM_STORAGE]));
	for (auto&& redisHost : REDIS_HOSTS)
	{
		PUT_PARAMETER(redisHost);
	}

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
	dumpToMattermost(argc, argv);

#pragma endregion

	return 0;
}

#pragma region HELPERS

template <class INPUT, class OUTPUT>
vector<OUTPUT> transform(const vector<INPUT>& input, function<OUTPUT(const INPUT&)> application)
{
	vector<OUTPUT> output;
	output.resize(input.size());
	transform(input.begin(), input.end(), output.begin(), application);

	return output;
}

string filename(string filename, int i)
{
	return string(FILES_DIR) + "/" + filename + (i > -1 ? ("-" + to_string(i)) : "") + ".bin";
}

wstring toWString(string input)
{
	wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.from_bytes(input);
}

inline void storeInputs(vector<pair<number, number>>& queries, vector<number>& oramBlockNumbers)
{
	ofstream statFile(filename(STATS_INPUT_FILE, -1));

	statFile << COUNT << endl;
	statFile << MIN_VALUE << endl;
	statFile << MAX_VALUE << endl;
	statFile << MAX_RANGE << endl;
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

inline void loadInputs(vector<pair<number, number>>& queries, vector<number>& oramBlockNumbers)
{
	ifstream statFile(filename(STATS_INPUT_FILE, -1));

	statFile >> COUNT >> MIN_VALUE >> MAX_VALUE >> MAX_RANGE;
	number blockNumber;
	for (number i = 0; i < ORAMS_NUMBER; i++)
	{
		statFile >> blockNumber;
		oramBlockNumbers.push_back(blockNumber);
	}

	statFile.close();

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
}

void addFakeRequests(vector<number>& blocks, number maxBlocks, number fakesNumber)
{
	// TODO possibly avoid sort
	// TODO it is supposd to be sorted already
	sort(blocks.begin(), blocks.end());

	for (auto j = 0uLL, inserted = 0uLL, block = 0uLL; inserted < fakesNumber; j++)
	{
		if (block < blocks.size() && blocks[block] == j)
		{
			block++;
			continue;
		}
		blocks.push_back(j % maxBlocks);
		inserted++;
	}
}

void printProfileStats(vector<profile>& profiles, number queries)
{
	if (profiles.size() == 0)
	{
		return;
	}

	auto profileAvg = [&profiles, &queries](function<bool(const profile&)> ifCount, function<number(const profile&)> getter) -> pair<number, number> {
		vector<profile> filtered;
		copy_if(profiles.begin(), profiles.end(), back_inserter(filtered), ifCount);

		auto values = transform<profile, number>(filtered, getter);
		auto sum	= accumulate(values.begin(), values.end(), 0LL);
		return {sum, sum / (queries == 0 ? values.size() : queries)};
	};

	for (auto&& mode : {L"in", L"out", L"all"})
	{
		auto ifCount = [&mode](const profile& v) -> bool {
			if (wcscmp(mode, L"in") == 0)
			{
				return get<0>(v);
			}
			else if (wcscmp(mode, L"out") == 0)
			{
				return !get<0>(v);
			}
			return true;
		};

		auto events							= profileAvg(ifCount, [](const profile& v) { return 1uLL; }).first;
		auto [batchSum, batchAverage]		= profileAvg(ifCount, [](const profile v) { return get<1>(v); });
		auto [sizeSum, sizeAverage]			= profileAvg(ifCount, [](const profile v) { return get<2>(v); });
		auto [overheadSum, overheadAverage] = profileAvg(ifCount, [](const profile v) { return get<3>(v); });

		auto denominator = queries == 0 ? L"req" : L"query";

		LOG(queries == 0 ? TRACE : DEBUG, boost::wformat(L"%3s: %6i req, %6i buckets (%4i / %s, %4i / ORAM), %8s (%8s / %s, %8s / ORAM), %7s (%7s / %s, %7s / ORAM)") % mode % events % batchSum % batchAverage % denominator % (batchSum / ORAMS_NUMBER) % bytesToString(sizeSum) % bytesToString(sizeAverage) % denominator % bytesToString(sizeSum / ORAMS_NUMBER) % timeToString(overheadSum) % timeToString(overheadAverage) % denominator % timeToString(overheadSum / ORAMS_NUMBER));
	}
	if (queries == 0)
	{
		LOG(TRACE, L"");
	}
}

void dumpToMattermost(int argc, char* argv[])
{
	if (DUMP_TO_MATTERMOST)
	{
		if (const char* env_hook = std::getenv("MATTERMOST_HOOK"))
		{
			auto hook = string(env_hook);

			stringstream cli;
			for (auto i = 0; i < argc; i++)
			{
				cli << argv[i] << " ";
			}
			auto cliStr = cli.str();

			stringstream ss;
			auto count = cliStr.size();
			auto lines = 0u;
			auto part  = 1;
			for (auto&& line : logLines)
			{
				count += line.size();
				lines++;
				ss << line << endl;

				if (count >= 16383 - 256 || lines == logLines.size() - 1)
				{
					auto partString = (lines == logLines.size() - 1) && part == 1 ? "" : boost::str(boost::format("**PART %i**\n") % part);

					exec(boost::str(boost::format("curl -s -i -X POST -H 'Content-Type: application/json' -d '{\"text\": \"%s\n`%s`\n```\n%s\n```\"}' %s") % partString % cliStr % ss.str() % hook));

					ss.str("");
					ss.clear();
					count = cliStr.size();
					part++;
				}
			}
		}
		else
		{
			LOG(ERROR, L"DUMP_TO_MATTERMOST is set to true, but hook is not set in MATTERMOST_HOOK. Will not post to Mattermost.");
		}
	}
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
		wcout << L"[" << put_time(localtime(&t), L"%d/%m/%Y %H:%M:%S") << L"] " << setw(10) << logLevelColors[level] << logLevelStrings[level] << L": " << message << RESET << endl;

		if (DUMP_TO_MATTERMOST)
		{
			stringstream ss;
			wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			ss << "[" << put_time(localtime(&t), "%d/%m/%Y %H:%M:%S") << "] " << setw(10) << converter.to_bytes(logLevelStrings[level]) << ": " << converter.to_bytes(message);
			logLines.push_back(ss.str());
		}

		if (FILE_LOGGING || DUMP_TO_MATTERMOST)
		{
			wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			logFile << "[" << put_time(localtime(&t), "%d/%m/%Y %H:%M:%S") << "] " << setw(10) << converter.to_bytes(logLevelStrings[level]) << ": " << converter.to_bytes(message) << endl;
		}
	}
	if (level == CRITICAL)
	{
		exit(1);
	}
}

#pragma endregion
