# Epsolute experiments

> These instructions are for developers.
> Instructions for general reproducibility will follow.

To install environment

```sh
byobu-enable
byobu

sudo apt update
sudo apt upgrade -y
sudo apt install -y git vim g++ make cmake libboost-all-dev libssl-dev

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

ssh-keygen
cat ~/.ssh/id_rsa.pub

git clone https://git.dbogatov.org/bu/epsolute/epsolute.git
git clone https://git.dbogatov.org/bu/epsolute/path-oram.git
git clone https://git.dbogatov.org/bu/epsolute/b-plus-tree.git
git clone https://git.dbogatov.org/bu/epsolute/experiments-scripts.git

cd ~/path-oram/path-oram
make clean && make shared CPPFLAGS="-DUSE_AEROSPIKE=false" -j8

cd ~/b-plus-tree/b-plus-tree/
make clean && make shared -j8

cd ~/dp-oram/dp-oram/
make clean copy-libs-dev && sudo make ldconfig && make main redis server -j8
```

To install ORAM service

```sh
cd experiments-scripts/configs/
git pull
sudo ln -s /home/$(whoami)/experiments-scripts/configs/oram.service /lib/systemd/system/oram.service
sudo systemctl enable oram.service
sudo systemctl start oram.service
sudo systemctl status oram.service
```

Creating a VM

```
gcloud beta compute --project=decoded-badge-279222 instances create dp-oram-server-1 --zone=us-east4-c --machine-type=n1-standard-16 --subnet=default --network-tier=PREMIUM --maintenance-policy=MIGRATE --service-account=449479826471-compute@developer.gserviceaccount.com --scopes=https://www.googleapis.com/auth/devstorage.read_only,https://www.googleapis.com/auth/logging.write,https://www.googleapis.com/auth/monitoring.write,https://www.googleapis.com/auth/servicecontrol,https://www.googleapis.com/auth/service.management.readonly,https://www.googleapis.com/auth/trace.append --image=orams-redises --image-project=decoded-badge-279222 --boot-disk-size=100GB --boot-disk-type=pd-standard --boot-disk-device-name=dp-oram-server-1 --no-shielded-secure-boot --shielded-vtpm --shielded-integrity-monitoring --reservation-affinity=any
```

Updating ORAM server

```
cd ~/path-oram/path-oram/
git pull
make clean shared CPPFLAGS="-DUSE_AEROSPIKE=false"
cd ~/b-plus-tree/b-plus-tree/
git pull
make shared -j8
cd ~/dp-oram/dp-oram/
git pull
make clean copy-libs-dev
sudo make ldconfig
make server
cd ~/experiments-scripts/configs/
git pull
sudo ln -s /home/$(whoami)/experiments-scripts/configs/oram.service /lib/systemd/system/oram.service
sudo systemctl enable oram.service
sudo systemctl start oram.service
sudo systemctl status oram.service
```

Updating Redis server

```
sudo systemctl enable redis-server-63{79,80,81,82,83,84,85,86}.service
sudo systemctl start redis-server-63{79,80,81,82,83,84,85,86}.service
sudo systemctl status redis-server-6379.service
```
