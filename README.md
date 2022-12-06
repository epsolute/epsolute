# Epsolute: how to reproduce on Azure

In these instructions we will create an Epsolute cluster in a signle region consisting of one client, two (or more) ORAM servers and two (or more) Redis servers.
We will download a dataset and run the queries on it.

To follow the instruction, make sure you can create enough VMs in your Azure subscription (request a quota increase if necessary).

## Deploy Epsolute client

The client machine is trusted and has the dataset to store.

### Provision the VM

1. Go to "Create a virtual machine"
1. Let the name be `client`
1. Set the image to `Ubuntu Server 20.04 LTS Gen2` (this is important, the scripts expect this image)
1. VM architecture: `x64`
1. Size `Standard_D4s_v3` (others may work, you can try yourself)
1. Authentication type choose the one you like (SSH is generally preferred)
1. Click "Next: Disks"
1. OS disk type: `Standard SSD` (cheaper this way)
1. Click "Next: Networking"
1. Virtual Network set to the same network for this and all other VMs
1. Click "Review + create" and "create"

### Build Epsolute

Once the deployment finishes, SSH into your VM and run the following:

```sh
# install build tools
sudo apt update
sudo apt upgrade -y
sudo apt install -y git vim g++ make cmake libboost-all-dev libssl-dev

# build from source: hiredis, redis++ and rpclib
cd /tmp/
git clone https://github.com/redis/hiredis.git
cd hiredis
make
sudo make install
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
mkdir compile
cd compile
cmake -DCMAKE_BUILD_TYPE=Release -DREDIS_PLUS_PLUS_CXX_STANDARD=17 ..
make
sudo make install
git clone https://github.com/rpclib/rpclib.git
cd rpclib
mkdir build
cd build
cmake ..
cmake --build .
sudo make install
cd ~

# download Epsolute code
git clone https://github.com/epsolute/epsolute.git
git clone https://github.com/epsolute/path-oram.git
git clone https://github.com/epsolute/b-plus-tree.git
git clone https://github.com/epsolute/experiments-scripts.git

# build PathORAM component
cd ~/path-oram/path-oram
make clean && make shared CPPFLAGS="-DUSE_AEROSPIKE=false" -j$(nproc)

# build B+ tree component
cd ~/b-plus-tree/b-plus-tree/
make clean && make shared -j$(nproc)

# build Epsolute component
cd ~/epsolute/dp-oram/
make clean copy-libs-dev && sudo make ldconfig && make main redis server -j$(nproc)

# test the binary
./bin/main -h
```

### Run local query

Run the following to test local-only execution on a small dummy dataset:

```sh
cd ~/epsolute/dp-oram
./bin/main -r false -v trace --dumpToMattermost false
```

If you've got a log of queries and success exit code, you're good to go!

## Deploy Redis servers for backend

First, follow "Provision the VM" from the "Deploy Epsolute client", except set name to `redis-1`.
Remember to use the same virtual network as for `client`.

We are going to open 8 Redis ports for use by client, and by default Azure keeps them closed.
So when you are viewing the `redis-1` VM page, go to "Networking" and click "Add inbound rule".
Leave source as "Any" (although for production, better to specify `client`'s IP) and put `6380-6387` in the "Destination port ranges".
Click "Add".

To setup the Redis services run the following

```sh
# install Redis
curl -fsSL https://packages.redis.io/gpg | sudo gpg --dearmor -o /usr/share/keyrings/redis-archive-keyring.gpg

echo "deb [signed-by=/usr/share/keyrings/redis-archive-keyring.gpg] https://packages.redis.io/deb $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/redis.list

sudo apt-get update
sudo apt upgrade -y
sudo apt-get install -y redis

# download scripts
cd ~
git clone https://github.com/epsolute/experiments-scripts.git

# install Redis services
cd ~/experiments-scripts/configs/
sudo ./put-redis.sh 6380 6387
```

In this example we will only use 4 services per VM, but in a similar fashion you can enable all 8:

```sh
# enable 4 service (enabling means run on startup)
# use this for all 8 if you want: 638{0,1,2,3,4,5,6,7}
sudo systemctl enable redis-server-638{0,1,2,3}.service

# start the services
sudo systemctl start redis-server-638{0,1,2,3}.service

# check status of one of them (sanity check)
sudo systemctl status redis-server-6380.service
```

Now, repeat the above steps for the second Redis VM, named `redis-2`.

## Test Epsolute with Redis

Now let's run an Epsolute query against the Redis servers.

On the `client` VM, runt he following:

```sh
./bin/main -r false -v trace --dumpToMattermost false -s redis -n 8 --redis tcp://redis-{1,2}:638{0,1,2,3}
```

Note in the above command we use storage type Redis `-s redis` instead of local, we use `-n 8` ORAMs (one per Redis service), and we specify all 8 Redis services by URLs.
Note that in Bash the following is expanded

```sh
tcp://redis-{1,2}:638{0,1,2,3}
# becomes
tcp://redis-1:6380 tcp://redis-1:6381 tcp://redis-1:6382 tcp://redis-1:6383 tcp://redis-2:6380 tcp://redis-2:6381 tcp://redis-2:6382 tcp://redis-2:6383
```

If you've got the output similar to local execution, you are good to go!

## Deploy ORAM servers

Lastly, in this step we will deploy a layer of ORAM servers for maximum efficiency and parallelization.
See [1, Figure 2].

First, follow "Provision the VM" from the "Deploy Epsolute client", except set name to `oram-1`.
Remember to use the same virtual network as for `client` and `redis-*` VMs.

Second, follow "Build Epsolute" from the "Deploy Epsolute client".
At the end of this sub-step you should be able to execute `./bin/main -h`.

Third, similar to instructions in "Deploy Redis servers for backend", open the RPC port `8787` for inbound traffic.

Now, SSH into the machine and run the ORAM server (assuming you have built the Epsolute per above instructions):

```sh
cd ~/epsolute/dp-oram/
./bin/oram-server # note: it should stay running!
```

Now, repeat the above steps for the second Redis VM, named `oram-2`.

## Run Epsolute with ORAM servers and Redis

We are finally ready to test-run the Epsolute with full infrastructure.
The client will connect to ORAM servers, which will connect to Redis servers.

Now, SSH into `client` VM and run the following:

```sh
cd ~/epsolute/dp-oram/
./bin/main -r false -s redis -n 8 -v trace --dumpToMattermost false --rpcHost oram-{1,2} --redis tcp://redis-{1,2}:638{0,1}
```

> Note, if the client hangs at any point, it is likely that there is a connection problem (see "Common issues" below).
> Keep checking the ORAM server VMs.
> If the process there fails, it will tell the reason.
> Run that process again.

If you've got the output similar to local execution, you are good to go!

## Run Epsolute with the real dataset

The dataset we used in the paper are here: [csr.bu.edu/dp-oram](http://csr.bu.edu/dp-oram).
In this step we will use one: `CA employees`.

Run the following to load the dataset:

```sh
cd ~/experiments-scripts/output/
wget http://csr.bu.edu/dp-oram/processed/dataset-CA-100000.csv
wget http://csr.bu.edu/dp-oram/processed/queries-CA-100000-1.0-uniform.csv
```

Now, run the Epsolute over this dataset and these queries (this may take a while):

```sh
cd ~/epsolute/dp-oram/
./bin/main -s redis -n 8 -v trace --dumpToMattermost false --dataset dataset-CA-100000 --queryset queries-CA-100000-1.0-uniform --rpcHost oram-{1,2} --redis tcp://redis-{1,2}:638{0,1}
```

Here is the expected output:

<details>
	<summary>Toggle to see</summary>

```sh
./bin/main -s redis -n 8 -v trace --dumpToMattermost false --dataset dataset-CA-100000 --queryset queries-CA-100000-1.0-uniform --rpcHost oram-{1,2} --redis tcp://redis-{1,2}:638{0,1}
[06/12/2022 16:05:29]      INFO: Generating indices...
[06/12/2022 16:05:30]      INFO: Constructing data set...
[06/12/2022 16:05:32]      INFO: COUNT = 100000
[06/12/2022 16:05:32]      INFO: GENERATE_INDICES = 1
[06/12/2022 16:05:32]      INFO: READ_INPUTS = 1
[06/12/2022 16:05:32]      INFO: ORAM_BLOCK_SIZE = 4096
[06/12/2022 16:05:32]      INFO: ORAM_LOG_CAPACITY = 14
[06/12/2022 16:05:32]      INFO: ORAMS_NUMBER = 8
[06/12/2022 16:05:32]      INFO: PARALLEL = 1
[06/12/2022 16:05:32]      INFO: ORAM_Z = 3
[06/12/2022 16:05:32]      INFO: TREE_BLOCK_SIZE = 3208
[06/12/2022 16:05:32]      INFO: USE_ORAMS = 1
[06/12/2022 16:05:32]      INFO: USE_ORAM_OPTIMIZATION = 1
[06/12/2022 16:05:32]      INFO: toWString(rpcHost) = oram-1
[06/12/2022 16:05:32]      INFO: toWString(rpcHost) = oram-2
[06/12/2022 16:05:32]      INFO: DISABLE_ENCRYPTION = 0
[06/12/2022 16:05:32]      INFO: PROFILE_STORAGE_REQUESTS = 0
[06/12/2022 16:05:32]      INFO: PROFILE_THREADS = 0
[06/12/2022 16:05:32]      INFO: REDIS_FLUSH_ALL = 0
[06/12/2022 16:05:32]      INFO: FILE_LOGGING = 0
[06/12/2022 16:05:32]      INFO: DUMP_TO_MATTERMOST = 0
[06/12/2022 16:05:32]      INFO: VIRTUAL_REQUESTS = 0
[06/12/2022 16:05:32]      INFO: BATCH_SIZE = 15000
[06/12/2022 16:05:32]      INFO: TWO_ATTRIBUTES = 0
[06/12/2022 16:05:32]      INFO: QUERY_MULTIPLE = 0
[06/12/2022 16:05:32]      INFO: SEED = 1305
[06/12/2022 16:05:32]      INFO: DP_K = 16
[06/12/2022 16:05:32]      INFO: DP_BETA = 20
[06/12/2022 16:05:32]      INFO: DP_EPSILON = 0.693
[06/12/2022 16:05:32]      INFO: DP_USE_GAMMA = 1
[06/12/2022 16:05:32]      INFO: DATASET_TAG = dataset-CA-100000
[06/12/2022 16:05:32]      INFO: QUERYSET_TAG = queries-CA-100000-1.0-uniform
[06/12/2022 16:05:32]      INFO: ORAM_BACKEND = Redis
[06/12/2022 16:05:32]      INFO: toWString(redisHost) = tcp://redis-1:6380
[06/12/2022 16:05:32]      INFO: toWString(redisHost) = tcp://redis-1:6381
[06/12/2022 16:05:32]      INFO: toWString(redisHost) = tcp://redis-2:6380
[06/12/2022 16:05:32]      INFO: toWString(redisHost) = tcp://redis-2:6381
[06/12/2022 16:05:32]      INFO: Loading ORAMs and B+ tree
[06/12/2022 16:05:58]      DEBUG: RPC batch loaded
[06/12/2022 16:05:59]      INFO: B+ tree size: 556 KB
[06/12/2022 16:05:59]      INFO: ORAMs size: 7104 KB (each of 8 ORAMs occupies 384 KB for position map and 504 KB for stash)
[06/12/2022 16:05:59]      INFO: Remote storage size: 1548 MB (each of 8 ORAMs occupies 193 MB for storage)
[06/12/2022 16:05:59]      INFO: DP_DOMAIN = 1132982
[06/12/2022 16:05:59]      INFO: numberToSalary(MIN_VALUE) = -22165.2
[06/12/2022 16:05:59]      INFO: numberToSalary(MAX_VALUE) = 1.11082e+06
[06/12/2022 16:05:59]      INFO: MAX_RANGE = 91384
[06/12/2022 16:05:59]      INFO: DP_BUCKETS = 1048576
[06/12/2022 16:05:59]      INFO: DP_LEVELS = 5
[06/12/2022 16:05:59]      INFO: DP_MU = 107
[06/12/2022 16:05:59]      INFO: Generating DP noise tree...
[06/12/2022 16:06:02]      INFO: DP tree has 1118480 elements
[06/12/2022 16:06:02]      INFO: Running 20 queries...
[06/12/2022 16:06:02]      TRACE: Query {203208.00, 294592.99} was transformed to {203207.93, 294593.04}, buckets [208583, 293159], added total of 7015 noisy records
[06/12/2022 16:06:05]      TRACE: Query: {before: 5156 μs, ORAMs: 3204 ms, after: 1800 ns}, threads: {min: 2408 ms (1002), max: 3196 ms (1002), avg: 2842 ms, stddev: 3408 ms}
[06/12/2022 16:06:05]      DEBUG: Query   1 /  20 : {203208.00, 294592.99} the real records   1001 ( +     0 padding, +  7015 noise,   8016 total) (3209 ms, or 3206 μs / record)
[06/12/2022 16:06:05]      TRACE: Query {170677.00, 182705.40} was transformed to {170676.35, 182705.52}, buckets [178475, 189607], added total of 6573 noisy records
[06/12/2022 16:06:08]      TRACE: Query: {before: 5161 μs, ORAMs: 3280 ms, after: 1500 ns}, threads: {min: 2516 ms ( 947), max: 3272 ms ( 947), avg: 3022 ms, stddev: 3402 ms}
[06/12/2022 16:06:08]      DEBUG: Query   2 /  20 : {170677.00, 182705.40} the real records   1001 ( +     2 padding, +  6573 noise,   7576 total) (3285 ms, or 3282 μs / record)
[06/12/2022 16:06:08]      TRACE: Query {197777.00, 250766.36} was transformed to {197776.27, 250767.04}, buckets [203556, 252598], added total of 8855 noisy records
[06/12/2022 16:06:12]      TRACE: Query: {before: 4893 μs, ORAMs: 4131 ms, after: 1700 ns}, threads: {min: 3223 ms (1232), max: 4126 ms (1232), avg: 3855 ms, stddev: 2163 ms}
[06/12/2022 16:06:12]      DEBUG: Query   3 /  20 : {197777.00, 250766.36} the real records   1001 ( +     0 padding, +  8855 noise,   9856 total) (4136 ms, or 4132 μs / record)
[06/12/2022 16:06:12]      TRACE: Query { 87936.00,  89350.12} was transformed to { 87935.21,  89350.66}, buckets [101898, 103207], added total of 5014 noisy records
[06/12/2022 16:06:15]      TRACE: Query: {before: 5093 μs, ORAMs: 2714 ms, after: 1800 ns}, threads: {min: 2348 ms ( 752), max: 2708 ms ( 752), avg: 2577 ms, stddev: 3721 ms}
[06/12/2022 16:06:15]      DEBUG: Query   4 /  20 : { 87936.00,  89350.12} the real records   1001 ( +     1 padding, +  5014 noise,   6016 total) (2719 ms, or 2717 μs / record)
[06/12/2022 16:06:15]      TRACE: Query {177087.00, 195048.09} was transformed to {177086.94, 195049.10}, buckets [184408, 201031], added total of 6639 noisy records
[06/12/2022 16:06:18]      TRACE: Query: {before: 5124 μs, ORAMs: 3313 ms, after: 1700 ns}, threads: {min: 2767 ms ( 955), max: 3305 ms ( 955), avg: 3059 ms, stddev: 3042 ms}
[06/12/2022 16:06:18]      DEBUG: Query   5 /  20 : {177087.00, 195048.09} the real records   1001 ( +     0 padding, +  6639 noise,   7640 total) (3318 ms, or 3315 μs / record)
[06/12/2022 16:06:18]      TRACE: Query { 82423.00,  83616.14} was transformed to { 82422.52,  83616.47}, buckets [96796, 97900], added total of 5706 noisy records
[06/12/2022 16:06:22]      TRACE: Query: {before: 5337 μs, ORAMs: 3095 ms, after: 2600 ns}, threads: {min: 2327 ms ( 845), max: 3089 ms ( 845), avg: 2866 ms, stddev: 3403 ms}
[06/12/2022 16:06:22]      DEBUG: Query   6 /  20 : { 82423.00,  83616.14} the real records   1002 ( +    52 padding, +  5706 noise,   6760 total) (3100 ms, or 3094 μs / record)
[06/12/2022 16:06:22]      TRACE: Query {162090.00, 169011.33} was transformed to {162089.65, 169012.39}, buckets [170528, 176934], added total of 4535 noisy records
[06/12/2022 16:06:24]      TRACE: Query: {before: 5119 μs, ORAMs: 2686 ms, after: 1500 ns}, threads: {min: 1906 ms ( 692), max: 2677 ms ( 692), avg: 2419 ms, stddev: 3727 ms}
[06/12/2022 16:06:24]      DEBUG: Query   7 /  20 : {162090.00, 169011.33} the real records   1001 ( +     0 padding, +  4535 noise,   5536 total) (2691 ms, or 2688 μs / record)
[06/12/2022 16:06:24]      TRACE: Query { 74975.00,  75826.35} was transformed to { 74974.66,  75827.17}, buckets [89903, 90691], added total of 3713 noisy records
[06/12/2022 16:06:27]      TRACE: Query: {before: 9126 μs, ORAMs: 2452 ms, after: 1400 ns}, threads: {min: 1928 ms ( 648), max: 2444 ms ( 648), avg: 2235 ms, stddev: 3723 ms}
[06/12/2022 16:06:27]      DEBUG: Query   8 /  20 : { 74975.00,  75826.35} the real records   1002 ( +   469 padding, +  3713 noise,   5184 total) (2462 ms, or 2457 μs / record)
[06/12/2022 16:06:27]      TRACE: Query {193322.00, 229632.22} was transformed to {193321.39, 229632.54}, buckets [199433, 233038], added total of 8551 noisy records
[06/12/2022 16:06:31]      TRACE: Query: {before: 4962 μs, ORAMs: 4165 ms, after: 1300 ns}, threads: {min: 2751 ms (1194), max: 4158 ms (1194), avg: 3620 ms, stddev: 2671 ms}
[06/12/2022 16:06:31]      DEBUG: Query   9 /  20 : {193322.00, 229632.22} the real records   1001 ( +     0 padding, +  8551 noise,   9552 total) (4170 ms, or 4166 μs / record)
[06/12/2022 16:06:31]      TRACE: Query {-15478.00,    200.86} was transformed to {-15479.07,    201.09}, buckets [6188, 20699], added total of 7090 noisy records
[06/12/2022 16:06:34]      TRACE: Query: {before: 5101 μs, ORAMs: 3475 ms, after: 1700 ns}, threads: {min: 2500 ms (1012), max: 3470 ms (1012), avg: 3100 ms, stddev: 3050 ms}
[06/12/2022 16:06:34]      DEBUG: Query  10 /  20 : {-15478.00,    200.86} the real records   1001 ( +     5 padding, +  7090 noise,   8096 total) (3480 ms, or 3477 μs / record)
[06/12/2022 16:06:34]      TRACE: Query { 39524.00,  40811.86} was transformed to { 39523.58,  40812.62}, buckets [57093, 58285], added total of 6950 noisy records
[06/12/2022 16:06:38]      TRACE: Query: {before: 5188 μs, ORAMs: 3451 ms, after: 2200 ns}, threads: {min: 2539 ms ( 994), max: 3443 ms ( 994), avg: 3125 ms, stddev: 3051 ms}
[06/12/2022 16:06:38]      DEBUG: Query  11 /  20 : { 39524.00,  40811.86} the real records   1001 ( +     1 padding, +  6950 noise,   7952 total) (3456 ms, or 3453 μs / record)
[06/12/2022 16:06:38]      TRACE: Query { -3459.00,    230.54} was transformed to { -3459.63,    231.35}, buckets [17312, 20727], added total of 5702 noisy records
[06/12/2022 16:06:41]      TRACE: Query: {before: 4860 μs, ORAMs: 2922 ms, after: 1500 ns}, threads: {min: 2096 ms ( 838), max: 2914 ms ( 838), avg: 2604 ms, stddev: 3728 ms}
[06/12/2022 16:06:41]      DEBUG: Query  12 /  20 : { -3459.00,    230.54} the real records   1001 ( +     1 padding, +  5702 noise,   6704 total) (2927 ms, or 2924 μs / record)
[06/12/2022 16:06:41]      TRACE: Query {147331.00, 150807.47} was transformed to {147330.08, 150808.19}, buckets [156868, 160086], added total of 5525 noisy records
[06/12/2022 16:06:44]      TRACE: Query: {before: 5197 μs, ORAMs: 2844 ms, after: 1600 ns}, threads: {min: 2209 ms ( 817), max: 2839 ms ( 817), avg: 2602 ms, stddev: 3726 ms}
[06/12/2022 16:06:44]      DEBUG: Query  13 /  20 : {147331.00, 150807.47} the real records   1001 ( +    10 padding, +  5525 noise,   6536 total) (2850 ms, or 2847 μs / record)
[06/12/2022 16:06:44]      TRACE: Query {-13445.00,    200.93} was transformed to {-13445.57,    201.09}, buckets [8070, 20699], added total of 6286 noisy records
[06/12/2022 16:06:47]      TRACE: Query: {before: 4678 μs, ORAMs: 3122 ms, after: 1800 ns}, threads: {min: 2532 ms ( 911), max: 3117 ms ( 911), avg: 2898 ms, stddev: 3401 ms}
[06/12/2022 16:06:47]      DEBUG: Query  14 /  20 : {-13445.00,    200.93} the real records   1001 ( +     1 padding, +  6286 noise,   7288 total) (3127 ms, or 3124 μs / record)
[06/12/2022 16:06:47]      TRACE: Query { 72849.00,  74042.47} was transformed to { 72848.24,  74043.27}, buckets [87935, 89040], added total of 3895 noisy records
[06/12/2022 16:06:49]      TRACE: Query: {before: 5015 μs, ORAMs: 2331 ms, after: 1700 ns}, threads: {min: 1810 ms ( 612), max: 2326 ms ( 612), avg: 2175 ms, stddev: 3724 ms}
[06/12/2022 16:06:49]      DEBUG: Query  15 /  20 : { 72849.00,  74042.47} the real records   1001 ( +     0 padding, +  3895 noise,   4896 total) (2337 ms, or 2334 μs / record)
[06/12/2022 16:06:49]      TRACE: Query {187092.00, 212451.28} was transformed to {187091.25, 212451.57}, buckets [193667, 217137], added total of 5663 noisy records
[06/12/2022 16:06:52]      TRACE: Query: {before: 4884 μs, ORAMs: 3084 ms, after: 1500 ns}, threads: {min: 2865 ms ( 833), max: 3076 ms ( 833), avg: 2951 ms, stddev: 3396 ms}
[06/12/2022 16:06:52]      DEBUG: Query  16 /  20 : {187092.00, 212451.28} the real records   1001 ( +     0 padding, +  5663 noise,   6664 total) (3089 ms, or 3086 μs / record)
[06/12/2022 16:06:52]      TRACE: Query {113540.00, 115530.30} was transformed to {113539.72, 115531.08}, buckets [125595, 127437], added total of 5893 noisy records
[06/12/2022 16:06:55]      TRACE: Query: {before: 5308 μs, ORAMs: 3202 ms, after: 1800 ns}, threads: {min: 2548 ms ( 862), max: 3194 ms ( 862), avg: 2990 ms, stddev: 3401 ms}
[06/12/2022 16:06:55]      DEBUG: Query  17 /  20 : {113540.00, 115530.30} the real records   1001 ( +     2 padding, +  5893 noise,   6896 total) (3207 ms, or 3204 μs / record)
[06/12/2022 16:06:55]      TRACE: Query { 95097.00,  96697.46} was transformed to { 95096.74,  96698.03}, buckets [108526, 110007], added total of 4007 noisy records
[06/12/2022 16:06:58]      TRACE: Query: {before: 5227 μs, ORAMs: 2430 ms, after: 1600 ns}, threads: {min: 1926 ms ( 626), max: 2421 ms ( 626), avg: 2248 ms, stddev: 3723 ms}
[06/12/2022 16:06:58]      DEBUG: Query  18 /  20 : { 95097.00,  96697.46} the real records   1001 ( +     0 padding, +  4007 noise,   5008 total) (2435 ms, or 2433 μs / record)
[06/12/2022 16:06:58]      TRACE: Query {163852.00, 171430.41} was transformed to {163851.94, 171430.54}, buckets [172159, 179172], added total of 7367 noisy records
[06/12/2022 16:07:01]      TRACE: Query: {before: 4925 μs, ORAMs: 3664 ms, after: 1500 ns}, threads: {min: 2901 ms (1046), max: 3658 ms (1046), avg: 3338 ms, stddev: 3045 ms}
[06/12/2022 16:07:01]      DEBUG: Query  19 /  20 : {163852.00, 171430.41} the real records   1001 ( +     0 padding, +  7367 noise,   8368 total) (3669 ms, or 3666 μs / record)
[06/12/2022 16:07:01]      TRACE: Query {118355.00, 120511.98} was transformed to {118354.41, 120512.16}, buckets [130051, 132047], added total of 6415 noisy records
[06/12/2022 16:07:05]      TRACE: Query: {before: 5163 μs, ORAMs: 3275 ms, after: 1600 ns}, threads: {min: 2731 ms ( 927), max: 3265 ms ( 927), avg: 2984 ms, stddev: 3401 ms}
[06/12/2022 16:07:05]      DEBUG: Query  20 /  20 : {118355.00, 120511.98} the real records   1001 ( +     0 padding, +  6415 noise,   7416 total) (3281 ms, or 3277 μs / record)
[06/12/2022 16:07:05]      INFO: Complete!
[06/12/2022 16:07:05]      INFO: For 20 queries: total: 62 s, average: 3147 ms / query, fastest thread: 2441 ms / query, 3144 μs / fetched item; (1001+27+6069=7098) records / query
[06/12/2022 16:07:05]      INFO: For 20 queries: ingress: 7267 MB (363 MB / query), egress: 7267 MB (363 MB / query), network usage / query: 726 MB, or 186% of DB
[06/12/2022 16:07:05]      INFO: Log written to ./results/1305--2022-12-06-16-05-29--2031999.json
```
</details>

The number after `CA-` is the dataset size in records.
Feel free to try larger sets on more powerful setup (choices are 100K, 1M and 10M).
The 1M records set took about 9 minutes to construct and about 10 seconds per query on this setup.

> Note, if you run on a larger dataset, you may want to use `--parallelRPCLoad 1` (or a number that is smaller than the number of ORAMs).
> This instructs the client to upload the data to ORAMs this many at a time.
> This is required because the `client` VM in this setup is small and has little memory.
> If we don't restrict upload parallelism here, it will OOM.

Each query file contains 100 queries.
You can use this to run all 100 (20 by default): `--queries 100`.

## Common issues

Here is the list of issues I think can arise:

- *Process on one VM cannot connect to another*
  - check that ports are open for inbound traffic on the VMs
  - check that the VMs are running
  - check that the receiving process on the VM is running and is listening to the port (`sudo netstat -tupln` may help)
    - especially `oram-server` process (it fails if tried to connect to non-existing Redis)
  - check that the hostname resolves (i.e., `ping redis-1`), if not, use **internal** IP
  - use `tcp://` prefix when connecting directly Redis
  - check that VMs are on the same internal network

## Tips on improving the reproducibility setup

First of all, deploying a VM manually, SSHing into it and running the build is tedious.
I suggest to setup the VM once, convert it to a deployable image (Azure can do that) and then deploy new VMs from this image.
This way the Epsolute will come preinstalled in the VM!

Second, keeping ORAM server alive manually from the console can also be avoided.
One way is to use a `systemctl` service for the ORAM server (similar to Redis).
You can find an example service in `experiment-scripts/configs/oram.service`.
You may want to change the usernames and paths, but otherwise this simple service should be enough to start (and restart) the ORAM server automatically.

[1]: Dmytro Bogatov, Georgios Kellaris, George Kollios, Kobbi Nissim, and Adam O'Neill. 2021. Epsolute: Efficiently Querying Databases While Providing Differential Privacy. In Proceedings of the 2021 ACM SIGSAC Conference on Computer and Communications Security (CCS '21), November 15-19, 2021, Virtual Event, Republic of Korea. ACM, New York, NY, USA, 15 pages. [doi.org/10.1145/3460120.3484786](https: //doi.org/10.1145/3460120.3484786) [PDF](https://d3g9eenuvjhozt.cloudfront.net/assets/docs/epsolute.pdf?build=accd8848)
