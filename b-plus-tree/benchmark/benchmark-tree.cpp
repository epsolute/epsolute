#include "definitions.h"
#include "tree.hpp"

#include <benchmark/benchmark.h>
#include <fstream>

using namespace std;

namespace BPlusTree
{
	enum BenchmarkStorageAdapterType
	{
		StorageAdapterTypeInMemory,
		StorageAdapterTypeFileSystem
	};

	class TreeBenchmark : public ::benchmark::Fixture
	{
		public:
		inline static number BLOCK_SIZE;
		inline static number COUNT;
		inline static const string FILE_NAME = "storage.bin";

		protected:
		Tree* tree;
		AbsStorageAdapter* storage;

		void Configure(number BLOCK_SIZE, number COUNT, BenchmarkStorageAdapterType type)
		{
			this->BLOCK_SIZE = BLOCK_SIZE;
			this->COUNT		 = COUNT;

			switch (type)
			{
				case StorageAdapterTypeInMemory:
					storage = new InMemoryStorageAdapter(BLOCK_SIZE);
					break;
				case StorageAdapterTypeFileSystem:
					storage = new FileSystemStorageAdapter(BLOCK_SIZE, FILE_NAME, true);
					break;
				default:
					throw Exception(boost::format("BenchmarkStorageAdapterType %1% is not implemented") % type);
			}
		}

		~TreeBenchmark() override
		{
			delete tree;
			delete storage;
		}
	};

	bytes random(int size)
	{
		uchar material[size];
		for (int i = 0; i < size; i++)
		{
			material[i] = (uchar)rand();
		}
		return bytes(material, material + size);
	}

	BENCHMARK_DEFINE_F(TreeBenchmark, PayloadSinglePath)
	(benchmark::State& state)
	{
		Configure(state.range(0), state.range(1), (BenchmarkStorageAdapterType)state.range(2));

		vector<pair<number, bytes>> data;
		for (number i = 0; i < COUNT; i++)
		{
			data.push_back({i, random(BLOCK_SIZE - 4 * sizeof(number))});
		}

		tree = new Tree(storage, data);

		for (auto _ : state)
		{
			benchmark::DoNotOptimize(tree->search(rand() % COUNT));
		}
	}

	BENCHMARK_DEFINE_F(TreeBenchmark, PayloadRange)
	(benchmark::State& state)
	{
		Configure(state.range(0), state.range(1), (BenchmarkStorageAdapterType)state.range(2));
		const auto range = 10;

		vector<pair<number, bytes>> data;
		for (number i = 0; i < COUNT; i++)
		{
			data.push_back({i, random(BLOCK_SIZE - 4 * sizeof(number))});
		}

		tree = new Tree(storage, data);

		for (auto _ : state)
		{
			number start = rand() % (COUNT - range);
			benchmark::DoNotOptimize(tree->search(start, start + range - 1));
		}
	}

	BENCHMARK_REGISTER_F(TreeBenchmark, PayloadSinglePath)
		->Args({64, 100000, StorageAdapterTypeInMemory})
		->Args({128, 100000, StorageAdapterTypeInMemory})
		->Args({256, 100000, StorageAdapterTypeInMemory})

		->Args({64, 100000, StorageAdapterTypeFileSystem})
		->Args({128, 100000, StorageAdapterTypeFileSystem})
		->Args({256, 100000, StorageAdapterTypeFileSystem})

		->Iterations(1 << 10)
		->Unit(benchmark::kMicrosecond);

	BENCHMARK_REGISTER_F(TreeBenchmark, PayloadRange)
		->Args({64, 100000, StorageAdapterTypeInMemory})
		->Args({128, 100000, StorageAdapterTypeInMemory})
		->Args({256, 100000, StorageAdapterTypeInMemory})

		->Args({64, 100000, StorageAdapterTypeFileSystem})
		->Args({128, 100000, StorageAdapterTypeFileSystem})
		->Args({256, 100000, StorageAdapterTypeFileSystem})

		->Iterations(1 << 10)
		->Unit(benchmark::kMicrosecond);
}

BENCHMARK_MAIN();
